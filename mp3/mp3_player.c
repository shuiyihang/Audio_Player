#include "mad.h"
#include "stream.h"
#include "frame.h"
#include "synth.h"

#include "playerconfig.h"


#include "esp_log.h"
#include "driver/i2s.h"
#include "string.h"

static const char *TAG = "mp3_player";

#define IIS_NUM     0

#define BUFSIZE     2106
struct buffer
{
    FILE *fp;
    unsigned int flen;
    unsigned int fpos;
    unsigned char fbuf[BUFSIZE];
    unsigned int fbsize;//fbuf中实际持有的数据
};

typedef struct buffer mp3_file;


static enum mad_flow input(void *data,
						  struct mad_stream *stream)
{
    mp3_file *mp3fp = (mp3_file *)data;
    int rem;//未处理数据

    int new_size;//需要的新数据

    if(mp3fp->fpos < mp3fp->flen){//还有数据读

        rem = stream->bufend - stream->next_frame;

        ESP_LOGI(TAG,"rem:%d \n",rem);

        memcpy(mp3fp->fbuf,mp3fp->fbuf + mp3fp->fbsize - rem, rem);

        new_size = BUFSIZE - rem;
        if(mp3fp->fpos + new_size > mp3fp->flen){
            new_size = mp3fp->flen - mp3fp->fpos;
        }

        fread(mp3fp->fbuf + rem,1,new_size,mp3fp->fp);
        mp3fp->fbsize = new_size + rem;

        mp3fp->fpos += new_size;

        mad_stream_buffer(stream, mp3fp->fbuf, mp3fp->fbsize);
        return MAD_FLOW_CONTINUE;

    }else{
        return MAD_FLOW_STOP;
    }
}

static inline
signed int scale(mad_fixed_t sample)
{
  /* round */
  sample += (1L << (MAD_F_FRACBITS - 16));

  /* clip */
  if (sample >= MAD_F_ONE)
    sample = MAD_F_ONE - 1;
  else if (sample < -MAD_F_ONE)
    sample = -MAD_F_ONE;

  /* quantize */
  return sample >> (MAD_F_FRACBITS + 1 - 16);
}

static
enum mad_flow output(void *data,
		     struct mad_header const *header,
		     struct mad_pcm *pcm)
{
    unsigned int nchannels, nsamples;
    mad_fixed_t const *left_ch, *right_ch;

    /* pcm->samplerate contains the sampling frequency */

    nchannels = pcm->channels;
    nsamples  = pcm->length;
    left_ch   = pcm->samples[0];
    right_ch  = pcm->samples[1];

    while (nsamples--) {
        signed int sample;

        /* output sample(s) in 16-bit signed little-endian PCM */

        sample = scale(*left_ch++);
        // putchar((sample >> 0) & 0xff);
        // putchar((sample >> 8) & 0xff);
        ESP_LOGI(TAG,"rem:%#x \n",(sample >> 0) & 0xff);

        if (nchannels == 2) {
        sample = scale(*right_ch++);
        // putchar((sample >> 0) & 0xff);
        // putchar((sample >> 8) & 0xff);
        }
    }

    return MAD_FLOW_CONTINUE;
}




static
enum mad_flow error(void *data,
		    struct mad_stream *stream,
		    struct mad_frame *frame)
{
  mp3_file *mp3fp = (mp3_file *)data;
 
//   fprintf(stderr, "decoding error 0x%04x (%s) at byte offset %u\n",
// 	  stream->error, mad_stream_errorstr(stream),
// 	  stream->this_frame - mp3fp->fbuf);

  /* return MAD_FLOW_BREAK here to stop decoding (and propagate an error) */

  return MAD_FLOW_CONTINUE;
}



static int decode(mp3_file *mp3fp)
{
    struct mad_decoder decoder;
    int result;

    mad_decoder_init(&decoder, mp3fp,
		   input, 0 /* header */, 0 /* filter */, output,
		   error, 0 /* message */);
    result = mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

    /* release the decoder */

    mad_decoder_finish(&decoder);

    return result;
}

void mp3_song_player(const char* const file_name)
{
    mp3_file *mp3fp;
    long fsta, fend;

    mp3fp = (mp3_file*)malloc(sizeof(mp3_file));
    mp3fp->fp = fopen(file_name, "r");

    fsta = ftell(mp3fp->fp);
    fseek(mp3fp->fp, 0, SEEK_END);
    fend = ftell(mp3fp->fp);

    fread(mp3fp->fbuf,1,BUFSIZE,mp3fp->fp);

    mp3fp->fpos = BUFSIZE;
    mp3fp->fbsize = BUFSIZE;
    mp3fp->flen = fend - fsta;

    decode(mp3fp);

    ESP_LOGI(TAG,"file size:%d k\n",mp3fp->flen/1024);

    fclose(mp3fp->fp);
}