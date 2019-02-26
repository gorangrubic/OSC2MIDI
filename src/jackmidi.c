/*-
 * Copyright (c) 2014 Spencer Jackson <ssjackson71@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <sysexits.h>
#include <errno.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include "midiseq.h"


typedef struct _MidiMessage
{
    jack_nframes_t	time;
    int		len;	/* Length of MIDI message, in bytes. */
    uint8_t	data[3];
} MidiMessage;

#define RINGBUFFER_SIZE		256*sizeof(MidiMessage)

/* Will emit a warning if time between jack callbacks is longer than this. */
#define MAX_TIME_BETWEEN_CALLBACKS	0.1

/* Will emit a warning if execution of jack callback takes longer than this. */
#define MAX_PROCESSING_TIME	0.01

typedef struct _jackseq
{
    jack_ringbuffer_t *ringbuffer_out;
    jack_ringbuffer_t *ringbuffer_in;
    jack_client_t	*jack_client;
    jack_port_t	*output_port;
    jack_port_t	*input_port;
    jack_port_t	*filter_in_port;
    jack_port_t	*filter_out_port;
}JACK_SEQ;

///////////////////////////////////////////////
//These functions operate in the JACK RT Thread
///////////////////////////////////////////////

double
get_time(void)
{
    double seconds;
    int ret;
    struct timeval tv;

    ret = gettimeofday(&tv, NULL);

    if (ret)
    {
        perror("gettimeofday");
        exit(EX_OSERR);
    }

    seconds = tv.tv_sec + tv.tv_usec / 1000000.0;

    return (seconds);
}

double
get_delta_time(void)
{
    static double previously = -1.0;
    double now;
    double delta;

    now = get_time();

    if (previously == -1.0)
    {
        previously = now;

        return (0);
    }

    delta = now - previously;
    previously = now;

    assert(delta >= 0.0);

    return (delta);
}


double
nframes_to_ms(jack_client_t* jack_client,jack_nframes_t nframes)
{
    jack_nframes_t sr;

    sr = jack_get_sample_rate(jack_client);

    assert(sr > 0);

    return ((nframes * 1000.0) / (double)sr);
}

void
queue_message(jack_ringbuffer_t* ringbuffer, MidiMessage *ev)
{
    int written;

    if (jack_ringbuffer_write_space(ringbuffer) < sizeof(*ev))
    {
        printf("Not enough space in the ringbuffer, MIDI LOST.");
        return;
    }

    written = jack_ringbuffer_write(ringbuffer, (char *)ev, sizeof(*ev));

    if (written != sizeof(*ev))
        printf("jack_ringbuffer_write failed, MIDI LOST.");
}

void
process_midi_input(JACK_SEQ* seq,jack_nframes_t nframes)
{
    int read, events, i;
    void *port_buffer;
    MidiMessage rev;
    jack_midi_event_t event;

    port_buffer = jack_port_get_buffer(seq->input_port, nframes);
    if (port_buffer == NULL)
    {
        printf("jack_port_get_buffer failed, cannot receive anything.");
        return;
    }

#ifdef JACK_MIDI_NEEDS_NFRAMES
    events = jack_midi_get_event_count(port_buffer, nframes);
#else
    events = jack_midi_get_event_count(port_buffer);
#endif

    for (i = 0; i < events; i++)
    {

#ifdef JACK_MIDI_NEEDS_NFRAMES
        read = jack_midi_event_get(&event, port_buffer, i, nframes);
#else
        read = jack_midi_event_get(&event, port_buffer, i);
#endif
        if (!read)
        {
            //successful event get

            if (event.size <= 3 && event.size >=1)
            {
                //not sysex or something

                //PUSH ONTO CIRCULAR BUFFER
                //not sure if its a true copy onto buffer, if not this won't work
                rev.len = event.size;
                rev.time = event.time;
                memcpy(rev.data, event.buffer, rev.len);
                queue_message(seq->ringbuffer_in,&rev);
            }
        }


    }
}

void
process_midi_filter(MIDI_SEQ* mseq,jack_nframes_t nframes)
{
    JACK_SEQ* seq = (JACK_SEQ*)mseq->driver;
    int read, events, i, j;
    uint8_t* buffer;
    int8_t filter = *mseq->filter;
    void *inport_buffer;
    void *outport_buffer;
    jack_midi_event_t event;

    inport_buffer = jack_port_get_buffer(seq->filter_in_port, nframes);
    if (inport_buffer == NULL)
    {
        printf("jack_port_get_buffer failed, cannot receive anything.");
        return;
    }
    outport_buffer = jack_port_get_buffer(seq->filter_out_port, nframes);
    if (outport_buffer == NULL)
    {
        printf("jack_port_get_buffer failed, cannot send anything.");
        return;
    }

#ifdef JACK_MIDI_NEEDS_NFRAMES
    jack_midi_clear_buffer(outport_buffer, nframes);
#else
    jack_midi_clear_buffer(outport_buffer);
#endif

    //check if filter shift amount has changed
    if(filter != mseq->old_filter)
    {
        uint8_t data[3];
        event.buffer = data;
        event.size = 3;
        //turn off all currently on notes and send new note-ons
        for(j=0; j<mseq->nnotes; j++)
        {
            int note = mseq->note[j]+mseq->old_filter;
            if (note < 0 || note > 127)
                // note out of range, skip
                continue;
            event.buffer[0] = mseq->notechan[j]&0xEF;//note on to note off
            event.buffer[1] = note;
            event.buffer[2] = 0;
#ifdef JACK_MIDI_NEEDS_NFRAMES
            buffer = jack_midi_event_reserve(outport_buffer, 0, 3, nframes);
#else
            buffer = jack_midi_event_reserve(outport_buffer, 0, 3);
#endif

            if (buffer == NULL)
            {
                //warn_from_jack_thread_context("jack_midi_event_reserve failed, NOTE LOST.");
                break;
            }

            memcpy(buffer, event.buffer, 3);
        }
        for(j=0; j<mseq->nnotes; j++)
        {
            int note = mseq->note[j]+filter;
            if (note < 0 || note > 127)
                // note out of range, skip
                continue;
            event.buffer[0] = mseq->notechan[j];//note on
            event.buffer[1] = note;
            event.buffer[2] = mseq->notevel[j];
#ifdef JACK_MIDI_NEEDS_NFRAMES
            buffer = jack_midi_event_reserve(outport_buffer, 0, 3, nframes);
#else
            buffer = jack_midi_event_reserve(outport_buffer, 0, 3);
#endif

            if (buffer == NULL)
            {
                //warn_from_jack_thread_context("jack_midi_event_reserve failed, NOTE LOST.");
                break;
            }

            memcpy(buffer, event.buffer, 3);
        }
        mseq->old_filter = filter;
    }

#ifdef JACK_MIDI_NEEDS_NFRAMES
    events = jack_midi_get_event_count(inport_buffer, nframes);
#else
    events = jack_midi_get_event_count(inport_buffer);
#endif


    for (i = 0; i < events; i++)
    {

#ifdef JACK_MIDI_NEEDS_NFRAMES
        read = jack_midi_event_get(&event, inport_buffer, i, nframes);
#else
        read = jack_midi_event_get(&event, inport_buffer, i);
#endif
        if (!read)
        {
            //successful event get

            //filter if note
            if (event.size <= 3 && event.size >=1)
            {
                //not sysex or something

                if((event.buffer[0]&0xF0) == 0x80 ||
                        ((event.buffer[0]&0xF0) == 0x90 && event.buffer[2] == 0))
                {
                    uint8_t on = mseq->note[0];
                    //note off event
                    //remove it from list
                    for(j=0; j<mseq->nnotes; j++)
                    {
                        //find it and shift everything above it down 1
                        if(j && on == event.buffer[1])
                        {
                            mseq->note[j-1] = mseq->note[j];
                            mseq->notechan[j-1] = mseq->notechan[j];
                            mseq->notevel[j-1] = mseq->notevel[j];
                        }
                        else if(j)
                        {
                            on = mseq->note[j];
                        }
                    }
                    if(on == event.buffer[1])
                    {
                        mseq->nnotes--;
                    }
                    int note = event.buffer[1]+filter;
                    if (note < 0 || note > 127)
                        // note out of range, skip
                        continue;
                    event.buffer[1] = note;
                }
                else if((event.buffer[0]&0xF0) == 0x90)
                {
                    //note on event
                    for(j=0; j<mseq->nnotes; j++)
                    {
                        //check if it's already on the list
                        if(mseq->note[j] == event.buffer[1])
                        {
                            break;
                        }
                    }
                    if(j == mseq->nnotes)
                    {
                        //add note to queue
                        mseq->notechan[mseq->nnotes] = event.buffer[0];
                        mseq->note[mseq->nnotes] = event.buffer[1];
                        mseq->notevel[mseq->nnotes++] = event.buffer[2];
                    }
                    int note = event.buffer[1]+filter;
                    if (note < 0 || note > 127)
                        // note out of range, skip
                        continue;
                    event.buffer[1] = note;
                }
                else if((event.buffer[0]&0xF0) == 0xA0)
                {
                    //polyphonic aftertouch event (just transpose)
                    int note = event.buffer[1]+filter;
                    if (note < 0 || note > 127)
                        // note out of range, skip
                        continue;
                    event.buffer[1] = note;
                }

            }

            //copy to output
#ifdef JACK_MIDI_NEEDS_NFRAMES
            buffer = jack_midi_event_reserve(outport_buffer, event.time, event.size, nframes);
#else
            buffer = jack_midi_event_reserve(outport_buffer, event.time, event.size);
#endif

            if (buffer == NULL)
            {
                //warn_from_jack_thread_context("jack_midi_event_reserve failed, NOTE LOST.");
                break;
            }

            memcpy(buffer, event.buffer, event.size);
        }

    }
}

void
process_midi_output(JACK_SEQ* seq,jack_nframes_t nframes)
{
    int read, t;
    uint8_t *buffer;
    void *port_buffer;
    jack_nframes_t last_frame_time;
    MidiMessage ev;

    last_frame_time = jack_last_frame_time(seq->jack_client);

    port_buffer = jack_port_get_buffer(seq->output_port, nframes);
    if (port_buffer == NULL)
    {
        printf("jack_port_get_buffer failed, cannot send anything.");
        return;
    }

#ifdef JACK_MIDI_NEEDS_NFRAMES
    jack_midi_clear_buffer(port_buffer, nframes);
#else
    jack_midi_clear_buffer(port_buffer);
#endif

    while (jack_ringbuffer_read_space(seq->ringbuffer_out))
    {
        read = jack_ringbuffer_peek(seq->ringbuffer_out, (char *)&ev, sizeof(ev));

        if (read != sizeof(ev))
        {
            //warn_from_jack_thread_context("Short read from the ringbuffer, possible note loss.");
            jack_ringbuffer_read_advance(seq->ringbuffer_out, read);
            continue;
        }

        t = ev.time + nframes - last_frame_time;

        /* If computed time is too much into the future, we'll need
           to send it later. */
        if (t >= (int)nframes)
            break;

        /* If computed time is < 0, we missed a cycle because of xrun. */
        if (t < 0)
            t = 0;

        jack_ringbuffer_read_advance(seq->ringbuffer_out, sizeof(ev));

#ifdef JACK_MIDI_NEEDS_NFRAMES
        buffer = jack_midi_event_reserve(port_buffer, t, ev.len, nframes);
#else
        buffer = jack_midi_event_reserve(port_buffer, t, ev.len);
#endif

        if (buffer == NULL)
        {
            //warn_from_jack_thread_context("jack_midi_event_reserve failed, NOTE LOST.");
            break;
        }

        memcpy(buffer, ev.data, ev.len);
    }
}

// in, i+o, i+o+t, o+t, out

int
process_callback(jack_nframes_t nframes, void *seqq)
{
    MIDI_SEQ* mseq = (MIDI_SEQ*)seqq;
    JACK_SEQ* seq = (JACK_SEQ*)mseq->driver;
#ifdef MEASURE_TIME
    if (get_delta_time() > MAX_TIME_BETWEEN_CALLBACKS)
        printf("Had to wait too long for JACK callback; scheduling problem?");
#endif

    if(mseq->usein)
        process_midi_input( seq,nframes );
    if(mseq->usefilter)
        process_midi_filter( mseq,nframes );
    if(mseq->useout)
        process_midi_output( seq,nframes );

#ifdef MEASURE_TIME
    if (get_delta_time() > MAX_PROCESSING_TIME)
        printf("Processing took too long; scheduling problem?");
#endif

    return (0);
}

///////////////////////////////////////////////
//these functions are executed in other threads
///////////////////////////////////////////////
void queue_midi(MIDI_SEQ* seqq, uint8_t msg[])
{
    MidiMessage ev;
    JACK_SEQ* seq = (JACK_SEQ*)seqq->driver;
    ev.len = 3;

    // At least with JackOSX, Jack will transmit the bytes verbatim, so make
    // sure that we look at the status byte and trim the message accordingly,
    // in order not to transmit any invalid MIDI data.
    switch (msg[0] & 0xf0)
    {
    case 0x80:
    case 0x90:
    case 0xa0:
    case 0xb0:
    case 0xe0:
        break; // 2 data bytes
    case 0xc0:
    case 0xd0:
        ev.len = 2; // 1 data byte
        break;
    case 0xf0: // system message
        switch (msg[0])
        {
        case 0xf2:
            break; // 2 data bytes
        case 0xf1:
        case 0xf3:
            ev.len = 2; // 1 data byte
            break;
        case 0xf6:
        case 0xf8:
        case 0xf9:
        case 0xfa:
        case 0xfb:
        case 0xfc:
        case 0xfe:
        case 0xff:
            ev.len = 1; // no data byte
            break;
        default:
            // ignore unknown (most likely sysex)
            return;
        }
        break;
    default:
        return; // not a valid MIDI message, bail out
    }

    ev.data[0] = msg[0];
    ev.data[1] = msg[1];
    ev.data[2] = msg[2];

    ev.time = jack_frame_time(seq->jack_client);
    queue_message(seq->ringbuffer_out,&ev);
}

int pop_midi(MIDI_SEQ* seqq, uint8_t msg[])
{
    int read;
    MidiMessage ev;
    JACK_SEQ* seq = (JACK_SEQ*)seqq->driver;

    if (jack_ringbuffer_read_space(seq->ringbuffer_in))
    {
        read = jack_ringbuffer_peek(seq->ringbuffer_in, (char *)&ev, sizeof(ev));

        if (read != sizeof(ev))
        {
            //warn_from_jack_thread_context("Short read from the ringbuffer, possible note loss.");
            jack_ringbuffer_read_advance(seq->ringbuffer_in, read);
            return -1;
        }

        jack_ringbuffer_read_advance(seq->ringbuffer_in, sizeof(ev));

        memcpy(msg,ev.data,ev.len);

        return ev.len;
    }
    else
        return 0;
}

////////////////////////////////
//this is run in the main thread
////////////////////////////////
int
init_midi_seq(MIDI_SEQ* mseq, uint8_t verbose, const char* clientname)
{
    int err;
    JACK_SEQ* seq;

    mseq->nnotes = 0;
    mseq->old_filter = 0;
    seq = (JACK_SEQ*)malloc(sizeof(JACK_SEQ));
    mseq->driver = seq;
    if(verbose)printf("opening client...\n");
    seq->jack_client = jack_client_open(clientname, JackNullOption, NULL);

    if (seq->jack_client == NULL)
    {
        printf("Could not connect to the JACK server; run jackd first?\n");
        free(seq);
        return 0;
    }

    if(verbose)printf("assigning process callback...\n");
    err = jack_set_process_callback(seq->jack_client, process_callback, (void*)mseq);
    if (err)
    {
        printf("Could not register JACK process callback.\n");
        free(seq);
        return 0;
    }

    if(mseq->usein)
    {

        if(verbose)printf("initializing JACK input: \ncreating ringbuffer...\n");
        seq->ringbuffer_in = jack_ringbuffer_create(RINGBUFFER_SIZE);

        if (seq->ringbuffer_in == NULL)
        {
            printf("Cannot create JACK ringbuffer.\n");
            free(seq);
            return 0;
        }

        jack_ringbuffer_mlock(seq->ringbuffer_in);

        seq->input_port = jack_port_register(seq->jack_client, "midi_in", JACK_DEFAULT_MIDI_TYPE,
                                             JackPortIsInput, 0);

        if (seq->input_port == NULL)
        {
            printf("Could not register JACK port.\n");
            free(seq);
            return 0;
        }
    }
    if(mseq->useout)
    {

        if(verbose)printf("initializing JACK output: \ncreating ringbuffer...\n");
        seq->ringbuffer_out = jack_ringbuffer_create(RINGBUFFER_SIZE);

        if (seq->ringbuffer_out == NULL)
        {
            printf("Cannot create JACK ringbuffer.\n");
            free(seq);
            return 0;
        }

        jack_ringbuffer_mlock(seq->ringbuffer_out);

        seq->output_port = jack_port_register(seq->jack_client, "midi_out", JACK_DEFAULT_MIDI_TYPE,
                                              JackPortIsOutput, 0);

        if (seq->output_port == NULL)
        {
            printf("Could not register JACK port.\n");
            free(seq);
            return 0;
        }
    }
    if(mseq->usefilter)
    {
        if(verbose)printf("initializing JACK midi filter in/out pair...\n");
        seq->filter_in_port = jack_port_register(seq->jack_client, "filter_in", JACK_DEFAULT_MIDI_TYPE,
                              JackPortIsInput, 0);
        seq->filter_out_port = jack_port_register(seq->jack_client, "filter_out", JACK_DEFAULT_MIDI_TYPE,
                               JackPortIsOutput, 0);

        if (seq->filter_in_port == NULL || seq->filter_out_port == NULL)
        {
            printf("Could not register JACK port.\n");
            free(seq);
            return 0;
        }
    }

    if (jack_activate(seq->jack_client))
    {
        printf("Cannot activate JACK client.\n");
        free(seq);
        return 0;
    }
    return 1;
}

void close_midi_seq(MIDI_SEQ* mseq)
{
    JACK_SEQ* seq = (JACK_SEQ*)mseq->driver;
    if(mseq->useout)jack_ringbuffer_free(seq->ringbuffer_out);
    if(mseq->usein)jack_ringbuffer_free(seq->ringbuffer_in);
    free(seq);
}
