/* HTAB = 4 */
/****************************************************************************
 * madlld.c -- A simple program decoding an MPEG audio stream to 16-bit		*
 * PCM from stdin to stdout. This program is just a simple sample			*
 * demonstrating how the low-level libmad API can be used.					*
 *--------------------------------------------------------------------------*
 * (c) 2001--2004 Bertrand Petit											*
 *																			*
 * Redistribution and use in source and binary forms, with or without		*
 * modification, are permitted provided that the following conditions		*
 * are met:																	*
 *																			*
 * 1. Redistributions of source code must retain the above copyright		*
 *    notice, this list of conditions and the following disclaimer.			*
 *																			*
 * 2. Redistributions in binary form must reproduce the above				*
 *    copyright notice, this list of conditions and the following			*
 *    disclaimer in the documentation and/or other materials provided		*
 *    with the distribution.												*
 * 																			*
 * 3. Neither the name of the author nor the names of its contributors		*
 *    may be used to endorse or promote products derived from this			*
 *    software without specific prior written permission.					*
 * 																			*
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''		*
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED		*
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A			*
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR		*
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,				*
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT			*
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF			*
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND		*
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,		*
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT		*
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF		*
 * SUCH DAMAGE.																*
 *																			*
 ****************************************************************************/

/*
 * $Name: v1_1p1 $
 * $Date: 2004/03/19 07:13:13 $
 * $Revision: 1.20 $
 */

/****************************************************************************
 * Includes																	*
 ****************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <math.h> /* for pow() and log10() */
#include <mad.h>
#include "bstdfile.h"
#include "esp_log.h"

#include "media_player.h"
#include "driver/i2s.h"

static const char *TAG = "mp3_player";

/* Should we use getopt() for command-line arguments parsing? */
#if (defined(unix) || defined (__unix__) || defined(__unix) || \
	 defined(HAVE_GETOPT)) \
	&& !defined(WITHOUT_GETOPT)
#include <unistd.h>
#define HAVE_GETOPT
#else
#include <ctype.h>
#undef HAVE_GETOPT
#endif

/****************************************************************************
 * Global variables.														*
 ****************************************************************************/

/* Keeps a pointer to the program invocation name for the error
 * messages.
 */
const char	*ProgName = "mp3_player";

/* This table represents the subband-domain filter characteristics. It
 * is initialized by the ParseArgs() function and is used as
 * coefficients against each subband samples when DoFilter is non-nul.
 */
mad_fixed_t	Filter[32];

/* DoFilter is non-nul when the Filter table defines a filter bank to
 * be applied to the decoded audio subbands.
 */
int			DoFilter=0;


extern media_play_ctl_t* play_ctl;

/****************************************************************************
 * Return an error string associated with a mad error code.					*
 ****************************************************************************/
/* Mad version 0.14.2b introduced the mad_stream_errorstr() function.
 * For previous library versions a replacement is provided below.
 */
#if (MAD_VERSION_MAJOR>=1) || \
    ((MAD_VERSION_MAJOR==0) && \
     (((MAD_VERSION_MINOR==14) && \
       (MAD_VERSION_PATCH>=2)) || \
      (MAD_VERSION_MINOR>14)))
#define MadErrorString(x) mad_stream_errorstr(x)
#else
static const char *MadErrorString(const struct mad_stream *Stream)
{
	switch(Stream->error)
	{
		/* Generic unrecoverable errors. */
		case MAD_ERROR_BUFLEN:
			return("input buffer too small (or EOF)");
		case MAD_ERROR_BUFPTR:
			return("invalid (null) buffer pointer");
		case MAD_ERROR_NOMEM:
			return("not enough memory");

		/* Frame header related unrecoverable errors. */
		case MAD_ERROR_LOSTSYNC:
			return("lost synchronization");
		case MAD_ERROR_BADLAYER:
			return("reserved header layer value");
		case MAD_ERROR_BADBITRATE:
			return("forbidden bitrate value");
		case MAD_ERROR_BADSAMPLERATE:
			return("reserved sample frequency value");
		case MAD_ERROR_BADEMPHASIS:
			return("reserved emphasis value");

		/* Recoverable errors */
		case MAD_ERROR_BADCRC:
			return("CRC check failed");
		case MAD_ERROR_BADBITALLOC:
			return("forbidden bit allocation value");
		case MAD_ERROR_BADSCALEFACTOR:
			return("bad scalefactor index");
		case MAD_ERROR_BADFRAMELEN:
			return("bad frame length");
		case MAD_ERROR_BADBIGVALUES:
			return("bad big_values count");
		case MAD_ERROR_BADBLOCKTYPE:
			return("reserved block_type");
		case MAD_ERROR_BADSCFSI:
			return("bad scalefactor selection info");
		case MAD_ERROR_BADDATAPTR:
			return("bad main_data_begin pointer");
		case MAD_ERROR_BADPART3LEN:
			return("bad audio data length");
		case MAD_ERROR_BADHUFFTABLE:
			return("bad Huffman table select");
		case MAD_ERROR_BADHUFFDATA:
			return("Huffman data overrun");
		case MAD_ERROR_BADSTEREO:
			return("incompatible block_type for JS");

		/* Unknown error. This switch may be out of sync with libmad's
		 * defined error codes.
		 */
		default:
			return("Unknown error code");
	}
}
#endif

/****************************************************************************
 * Converts a sample from libmad's fixed point number format to a signed	*
 * short (16 bits).															*
 ****************************************************************************/
static signed short MadFixedToSshort(mad_fixed_t Fixed)
{
	/* A fixed point number is formed of the following bit pattern:
	 *
	 * SWWWFFFFFFFFFFFFFFFFFFFFFFFFFFFF
	 * MSB                          LSB
	 * S ==> Sign (0 is positive, 1 is negative)
	 * W ==> Whole part bits
	 * F ==> Fractional part bits
	 *
	 * This pattern contains MAD_F_FRACBITS fractional bits, one
	 * should alway use this macro when working on the bits of a fixed
	 * point number. It is not guaranteed to be constant over the
	 * different platforms supported by libmad.
	 *
	 * The signed short value is formed, after clipping, by the least
	 * significant whole part bit, followed by the 15 most significant
	 * fractional part bits. Warning: this is a quick and dirty way to
	 * compute the 16-bit number, madplay includes much better
	 * algorithms.
	 */

	/* Clipping */
	if(Fixed>=MAD_F_ONE)
		return(SHRT_MAX);
	if(Fixed<=-MAD_F_ONE)
		return(-SHRT_MAX);

	/* Conversion. */
	Fixed=Fixed>>(MAD_F_FRACBITS-15);
	return((signed short)Fixed);
}

/****************************************************************************
 * Print human readable informations about an audio MPEG frame.				*
 ****************************************************************************/
static int PrintFrameInfo(FILE *fp, struct mad_header *Header)
{
	const char	*Layer,
				*Mode,
				*Emphasis;

	/* Convert the layer number to it's printed representation. */
	switch(Header->layer)
	{
		case MAD_LAYER_I:
			Layer="I";
			break;
		case MAD_LAYER_II:
			Layer="II";
			break;
		case MAD_LAYER_III:
			Layer="III";
			break;
		default:
			Layer="(unexpected layer value)";
			break;
	}

	/* Convert the audio mode to it's printed representation. */
	switch(Header->mode)
	{
		case MAD_MODE_SINGLE_CHANNEL:
			Mode="single channel";
			break;
		case MAD_MODE_DUAL_CHANNEL:
			Mode="dual channel";
			break;
		case MAD_MODE_JOINT_STEREO:
			Mode="joint (MS/intensity) stereo";
			break;
		case MAD_MODE_STEREO:
			Mode="normal LR stereo";
			break;
		default:
			Mode="(unexpected mode value)";
			break;
	}

	/* Convert the emphasis to it's printed representation. Note that
	 * the MAD_EMPHASIS_RESERVED enumeration value appeared in libmad
	 * version 0.15.0b.
	 */
	switch(Header->emphasis)
	{
		case MAD_EMPHASIS_NONE:
			Emphasis="no";
			break;
		case MAD_EMPHASIS_50_15_US:
			Emphasis="50/15 us";
			break;
		case MAD_EMPHASIS_CCITT_J_17:
			Emphasis="CCITT J.17";
			break;
#if (MAD_VERSION_MAJOR>=1) || \
	((MAD_VERSION_MAJOR==0) && (MAD_VERSION_MINOR>=15))
		case MAD_EMPHASIS_RESERVED:
			Emphasis="reserved(!)";
			break;
#endif
		default:
			Emphasis="(unexpected emphasis value)";
			break;
	}
	ESP_LOGI(TAG,"%s: %lu kb/s audio MPEG layer %s stream %s CRC, "
			"%s with %s emphasis at %d Hz sample rate\n",
			ProgName,Header->bitrate,Layer,
			Header->flags&MAD_FLAG_PROTECTION?"with":"without",
			Mode,Emphasis,Header->samplerate);
	return(ferror(fp));
}

/****************************************************************************
 * Applies a frequency-domain filter to audio data in the subband-domain.	*
 ****************************************************************************/
static void ApplyFilter(struct mad_frame *Frame)
{
	int	Channel,
		Sample,
		Samples,
		SubBand;

	/* There is two application loops, each optimized for the number
	 * of audio channels to process. The first alternative is for
	 * two-channel frames, the second is for mono-audio.
	 */
	Samples=MAD_NSBSAMPLES(&Frame->header);
	if(Frame->header.mode!=MAD_MODE_SINGLE_CHANNEL)
		for(Channel=0;Channel<2;Channel++)
			for(Sample=0;Sample<Samples;Sample++)
				for(SubBand=0;SubBand<32;SubBand++)
					Frame->sbsample[Channel][Sample][SubBand]=
						mad_f_mul(Frame->sbsample[Channel][Sample][SubBand],
								  Filter[SubBand]);
	else
		for(Sample=0;Sample<Samples;Sample++)
			for(SubBand=0;SubBand<32;SubBand++)
				Frame->sbsample[0][Sample][SubBand]=
					mad_f_mul(Frame->sbsample[0][Sample][SubBand],
							  Filter[SubBand]);
}



signed short fix_vol(unsigned short sample,unsigned char vol,unsigned char volmax)
{
	float delta = 1.0 * vol / volmax;
    unsigned short temp;
    if(sample > 0x7fff){//音频的负幅度
        temp = 0xffff - (0xffff - sample) * delta;
        if(temp < 0x8000){
            temp = 0x8000;
        }
    }else{//正幅度
        temp = sample * delta ;
        if(temp > 0x7fff){
            temp = 0x7fff;
        }
    }
    
	return temp;
}

/****************************************************************************
 * Main decoding loop. This is where mad is used.							*
 ****************************************************************************/
#define INPUT_BUFFER_SIZE	(4096)	//	192kb可能开始会出错，128正常
#define OUTPUT_BUFFER_SIZE	2048 /* Must be an integer multiple of 4. */

struct mad_stream	Stream;
struct mad_frame	Frame;
struct mad_synth	Synth;

unsigned char		InputBuffer[INPUT_BUFFER_SIZE+MAD_BUFFER_GUARD];

unsigned short		OutputBuffer[2][OUTPUT_BUFFER_SIZE];

int sample_rate_copy = 44100;

unsigned char use_buf = 0;


static int MpegAudioDecoder(FIL *InputFp)
{
	mad_timer_t			Timer;
	unsigned short			*OutputPtr = &OutputBuffer[use_buf][0];

	unsigned char			*GuardPtr=NULL;
	const unsigned short	*OutputBufferEnd = OutputPtr + OUTPUT_BUFFER_SIZE;
	int					Status=0,
						i;
	unsigned long		FrameCount=0;
	bstdfile_t			*BstdFile;

	size_t bytesWritten;

	/* First the structures used by libmad must be initialized. */
	mad_stream_init(&Stream);
	mad_frame_init(&Frame);
	mad_synth_init(&Synth);
	mad_timer_reset(&Timer);

	/* Decoding options can here be set in the options field of the
	 * Stream structure.
	 */

	/* {1} When decoding from a file we need to know when the end of
	 * the file is reached at the same time as the last bytes are read
	 * (see also the comment marked {3} bellow). Neither the standard
	 * C fread() function nor the POSIX read() system call provides
	 * this feature. We thus need to perform our reads through an
	 * interface having this feature, this is implemented here by the
	 * bstdfile.c module.
	 */
	

	// ESP_LOGI(TAG,"free mem: %d in line: %d\n",uxTaskGetStackHighWaterMark(NULL),__LINE__);


	BstdFile=NewBstdFile(InputFp);
	if(BstdFile==NULL)
	{
		ESP_LOGI(TAG,"%s: can't create a new bstdfile_t (%s).\n",
				ProgName,strerror(errno));
		return(1);
	}

	/* This is the decoding loop. */
	do
	{
		/* The input bucket must be filled if it becomes empty or if
		 * it's the first execution of the loop.
		 */
		// ESP_LOGI(TAG,"free mem: %d in line: %d\n",uxTaskGetStackHighWaterMark(NULL),__LINE__);
		if(Stream.buffer==NULL || Stream.error==MAD_ERROR_BUFLEN)
		{
			size_t			ReadSize,
							Remaining;
			unsigned char	*ReadStart;

			// if(Stream.error == MAD_ERROR_BUFLEN){
			// 	printf("erro buflen:%d \n",__LINE__);
			// }

			/* {2} libmad may not consume all bytes of the input
			 * buffer. If the last frame in the buffer is not wholly
			 * contained by it, then that frame's start is pointed by
			 * the next_frame member of the Stream structure. This
			 * common situation occurs when mad_frame_decode() fails,
			 * sets the stream error code to MAD_ERROR_BUFLEN, and
			 * sets the next_frame pointer to a non NULL value. (See
			 * also the comment marked {4} bellow.)
			 *
			 * When this occurs, the remaining unused bytes must be
			 * put back at the beginning of the buffer and taken in
			 * account before refilling the buffer. This means that
			 * the input buffer must be large enough to hold a whole
			 * frame at the highest observable bit-rate (currently 448
			 * kb/s). XXX=XXX Is 2016 bytes the size of the largest
			 * frame? (448000*(1152/32000))/8
			 */
			if(Stream.next_frame!=NULL)
			{
				Remaining=Stream.bufend - Stream.next_frame;
				memmove(InputBuffer,Stream.next_frame,Remaining);
				ReadStart=InputBuffer+Remaining;
				ReadSize= INPUT_BUFFER_SIZE- Remaining;
			}
			else
				ReadSize=INPUT_BUFFER_SIZE,
					ReadStart=InputBuffer,
					Remaining=0;

			/* Fill-in the buffer. If an error occurs print a message
			 * and leave the decoding loop. If the end of stream is
			 * reached we also leave the loop but the return status is
			 * left untouched.
			 */
			// printf("read_size:%d \n",ReadSize);
			ReadSize = BstdRead(ReadStart,1,ReadSize,BstdFile);
			if(ReadSize<=0)
			{
				if(BstdFileEofP(BstdFile)){
					ESP_LOGI(TAG,"%s: end of input stream\n",ProgName);
					break;
				}
			}

			/* {3} When decoding the last frame of a file, it must be
			 * followed by MAD_BUFFER_GUARD zero bytes if one wants to
			 * decode that last frame. When the end of file is
			 * detected we append that quantity of bytes at the end of
			 * the available data. Note that the buffer can't overflow
			 * as the guard size was allocated but not used the the
			 * buffer management code. (See also the comment marked
			 * {1}.)
			 *
			 * In a message to the mad-dev mailing list on May 29th,
			 * 2001, Rob Leslie explains the guard zone as follows:
			 *
			 *    "The reason for MAD_BUFFER_GUARD has to do with the
			 *    way decoding is performed. In Layer III, Huffman
			 *    decoding may inadvertently read a few bytes beyond
			 *    the end of the buffer in the case of certain invalid
			 *    input. This is not detected until after the fact. To
			 *    prevent this from causing problems, and also to
			 *    ensure the next frame's main_data_begin pointer is
			 *    always accessible, MAD requires MAD_BUFFER_GUARD
			 *    (currently 8) bytes to be present in the buffer past
			 *    the end of the current frame in order to decode the
			 *    frame."
			 */
			if(BstdFileEofP(BstdFile))
			{
				GuardPtr=ReadStart+ReadSize;
				memset(GuardPtr,0,MAD_BUFFER_GUARD);
				ReadSize+=MAD_BUFFER_GUARD;
			}

			/* Pipe the new buffer content to libmad's stream decoder
             * facility.
			 */
			mad_stream_buffer(&Stream,InputBuffer,ReadSize+Remaining);
			Stream.error=0;
		}

		/* Decode the next MPEG frame. The streams is read from the
		 * buffer, its constituents are break down and stored the the
		 * Frame structure, ready for examination/alteration or PCM
		 * synthesis. Decoding options are carried in the Frame
		 * structure from the Stream structure.
		 *
		 * Error handling: mad_frame_decode() returns a non zero value
		 * when an error occurs. The error condition can be checked in
		 * the error member of the Stream structure. A mad error is
		 * recoverable or fatal, the error status is checked with the
		 * MAD_RECOVERABLE macro.
		 *
		 * {4} When a fatal error is encountered all decoding
		 * activities shall be stopped, except when a MAD_ERROR_BUFLEN
		 * is signaled. This condition means that the
		 * mad_frame_decode() function needs more input to complete
		 * its work. One shall refill the buffer and repeat the
		 * mad_frame_decode() call. Some bytes may be left unused at
		 * the end of the buffer if those bytes forms an incomplete
		 * frame. Before refilling, the remaining bytes must be moved
		 * to the beginning of the buffer and used for input for the
		 * next mad_frame_decode() invocation. (See the comments
		 * marked {2} earlier for more details.)
		 *
		 * Recoverable errors are caused by malformed bit-streams, in
		 * this case one can call again mad_frame_decode() in order to
		 * skip the faulty part and re-sync to the next frame.
		 */
		if(mad_frame_decode(&Frame,&Stream))
		{
			if(MAD_RECOVERABLE(Stream.error))
			{
				/* Do not print a message if the error is a loss of
				 * synchronization and this loss is due to the end of
				 * stream guard bytes. (See the comments marked {3}
				 * supra for more informations about guard bytes.)
				 */
				if(Stream.error!=MAD_ERROR_LOSTSYNC ||
				   Stream.this_frame!=GuardPtr)
				{
					// fprintf(stderr,"%s: recoverable frame level error (%s)\n",
					// 		ProgName,MadErrorString(&Stream));
					// fflush(stderr);
				}
				continue;
			}
			else
				if(Stream.error==MAD_ERROR_BUFLEN)
					continue;
				else
				{
					// fprintf(stderr,"%s: unrecoverable frame level error (%s).\n",
					// 		ProgName,MadErrorString(&Stream));
					Status=1;
					break;
				}
		}

		/* The characteristics of the stream's first frame is printed
		 * on stderr. The first frame is representative of the entire
		 * stream.
		 */
		if(FrameCount==0)
			if(PrintFrameInfo(stderr,&Frame.header))
			{
				Status=1;
				break;
			}

		/* Accounting. The computed frame duration is in the frame
		 * header structure. It is expressed as a fixed point number
		 * whole data type is mad_timer_t. It is different from the
		 * samples fixed point format and unlike it, it can't directly
		 * be added or subtracted. The timer module provides several
		 * functions to operate on such numbers. Be careful there, as
		 * some functions of libmad's timer module receive some of
		 * their mad_timer_t arguments by value!
		 */
		FrameCount++;
		mad_timer_add(&Timer,Frame.header.duration);

		/* Between the frame decoding and samples synthesis we can
		 * perform some operations on the audio data. We do this only
		 * if some processing was required. Detailed explanations are
		 * given in the ApplyFilter() function.
		 */
		if(DoFilter)
			ApplyFilter(&Frame);

		/* Once decoded the frame is synthesized to PCM samples. No errors
		 * are reported by mad_synth_frame();
		 */
		mad_synth_frame(&Synth,&Frame);



		//media ctl
		if(play_ctl->is_switch){
			play_ctl->is_switch = 0;
			break;
		}
		if(play_ctl->is_exit){
			break;
		}

		if(play_ctl->is_pause){
			i2s_stop(0);
			while(play_ctl->is_pause){
				vTaskDelay(pdMS_TO_TICKS(20));
			}
			i2s_start(0);
		}

		


		/* Synthesized samples must be converted from libmad's fixed
		 * point number to the consumer format. Here we use unsigned
		 * 16 bit big endian integers on two channels. Integer samples
		 * are temporarily stored in a buffer that is flushed when
		 * full.
		 */
		for(i=0;i<Synth.pcm.length;i++)
		{
			signed short	Sample;

			unsigned char en_val = 8;

			extern int16_t get_value(void);
			signed char delat = get_value();
			if(delat <= -8){
				en_val = 0;
			}else if(delat >=8){
				en_val = 16;
			}else{
				en_val = en_val + delat;
			}
			/* Left channel */
			Sample=MadFixedToSshort(Synth.pcm.samples[0][i]);
			Sample = fix_vol(Sample,en_val,8);
			*(OutputPtr++)=Sample;

			/* Right channel. If the decoded stream is monophonic then
			 * the right output channel is the same as the left one.
			 */
			if(MAD_NCHANNELS(&Frame.header)==2){
				Sample=MadFixedToSshort(Synth.pcm.samples[1][i]);
				Sample = fix_vol(Sample,en_val,8);
			}
			*(OutputPtr++)=Sample;
			/* Flush the output buffer if it is full. */
			if(OutputPtr == OutputBufferEnd)
			{
				if(Synth.pcm.samplerate != sample_rate_copy){
					
					sample_rate_copy = Synth.pcm.samplerate;
					i2s_set_sample_rates(0, sample_rate_copy);
					
				}
				i2s_write(0, &OutputBuffer[use_buf][0], OUTPUT_BUFFER_SIZE * sizeof(short), &bytesWritten, 1000 / portTICK_PERIOD_MS);
				use_buf ^= 1;
				OutputPtr = &OutputBuffer[use_buf][0];
				OutputBufferEnd = OutputPtr + OUTPUT_BUFFER_SIZE;
			}
		}
	}while(1);

	/* The input file was completely read; the memory allocated by our
	 * reading module must be reclaimed.
	 */
	BstdFileDestroy(BstdFile);

	/* Mad is no longer used, the structures that were initialized must
     * now be cleared.
	 */
	mad_synth_finish(&Synth);
	mad_frame_finish(&Frame);
	mad_stream_finish(&Stream);

	/* If the output buffer is not empty and no error occurred during
     * the last write, then flush it.
	 */
	if(OutputPtr != &OutputBuffer[use_buf][0] && Status!=2)
	{
		size_t	BufferSize = OutputPtr - &OutputBuffer[use_buf][0];

		i2s_write(0, &OutputBuffer[use_buf][0], BufferSize * sizeof(short), &bytesWritten, 1000 / portTICK_PERIOD_MS);

		ESP_LOGI(TAG,"write Remaining data!\n");
	}

	/* Accounting report if no error occurred. */
	if(!Status)
	{
		char	Buffer[80];

		/* The duration timer is converted to a human readable string
		 * with the versatile, but still constrained mad_timer_string()
		 * function, in a fashion not unlike strftime(). The main
		 * difference is that the timer is broken into several
		 * values according some of it's arguments. The units and
		 * fracunits arguments specify the intended conversion to be
		 * executed.
		 *
		 * The conversion unit (MAD_UNIT_MINUTES in our example) also
		 * specify the order and kind of conversion specifications
		 * that can be used in the format string.
		 *
		 * It is best to examine libmad's timer.c source-code for details
		 * of the available units, fraction of units, their meanings,
		 * the format arguments, etc.
		 */
		mad_timer_string(Timer,Buffer,"%lu:%02lu.%03u",
						 MAD_UNITS_MINUTES,MAD_UNITS_MILLISECONDS,0);
		ESP_LOGI(TAG,"%s: %lu frames decoded (%s).\n",
				ProgName,FrameCount,Buffer);
	}

	/* That's the end of the world (in the H. G. Wells way). */
	return(Status);
}



/****************************************************************************
 * Program entry point.														*
 ****************************************************************************/
int madlld_song_player(FIL* fp)
{
	int		Status = 0;

	Status = MpegAudioDecoder(fp);
	/* All done. */
	return(Status);
}

/*  LocalWords:  BUFLEN HTAB madlld libmad bstdfile getopt subband ParseArgs JS
 */
/*  LocalWords:  DoFilter subbands errorstr bitrate scalefactor libmad's lu kb
 */
/*  LocalWords:  SWWWFFFFFFFFFFFFFFFFFFFFFFFFFFFF FRACBITS madplay fread synth
 */
/*  LocalWords:  ApplyFilter strftime fracunits atten tSets tRequest tThe tas
 */
/*  LocalWords:  ttransmitted unix todouble tofixed
 */
/*
 * Local Variables:
 * tab-width: 4
 * End:
 */

/****************************************************************************
 * End of file madlld.c														*
 ****************************************************************************/
