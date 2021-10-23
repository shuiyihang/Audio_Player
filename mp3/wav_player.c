#include "wav_player.h"
#include "esp_log.h"
#include "driver/i2s.h"

static const char *TAG = "wav_player";

static FILE *g_file = NULL;

static const int i2s_num = 0; // i2s port number


// static void IRAM_ATTR i2s_isr(void *arg)
// {
//     BaseType_t HPTaskAwoken = pdFALSE;

//     typeof(I2S0.int_st) status = I2S0.int_st;

//     if (status.val == 0) {
//         return;
//     }

//     if (status.out_eof) {
//         use_buf1 = !use_buf1;
//     }

//     if (HPTaskAwoken == pdTRUE) {
//         portYIELD_FROM_ISR();
//     }
// }



uint8_t wav_decode_init(const char* const file_name,__wavctrl* wav_info)
{
    ChunkRIFF *riff;
    ChunkFMT *fmt;
    ChunkFACT *fact;
    ChunkDATA *data;
    uint8_t res = 0;
    uint8_t *buf = malloc(512);
    if(!buf){
        res = 1;
        ESP_LOGI(TAG, "buf malloc failed \n");
        return res;
    }
    FILE *file = fopen(file_name, "r");
    if(file){
        fread(buf,sizeof(char),512,file);

        riff = (ChunkRIFF *)buf;
        if(riff->Format == 0X45564157){//wav标识
            fmt = (ChunkFMT *)(buf+12);
            fact=(ChunkFACT *)(buf+12+8+fmt->ChunkSize);
            if(fact->ChunkID==0X74636166||fact->ChunkID==0X5453494C)
                wav_info->datastart = 12+8+fmt->ChunkSize+8+fact->ChunkSize;//具有fact/LIST块的时候(未测试)
            else 
                wav_info->datastart = 12+8+fmt->ChunkSize;
            data = (ChunkDATA*)(buf+wav_info->datastart);
            if(data->ChunkID==0X61746164){//解析成功!
                ESP_LOGI(TAG, "wav file decoder success \n");
                wav_info->audioformat = fmt->AudioFormat;
                wav_info->nchannels = fmt->NumOfChannels;
                wav_info->samplerate = fmt->SampleRate;//采样率48k
                wav_info->bitrate = fmt->ByteRate * 8;
                wav_info->blockalign = fmt->BlockAlign;
                wav_info->bps = fmt->BitsPerSample;//位数 16、24、32

                wav_info->datasize = data->ChunkSize;//数据区的开头以后的数据总数
                wav_info->datastart = wav_info->datastart + 8;//数据流开始的地方
            }
        }
    }else{
        res = 2;
        ESP_LOGI(TAG, "file open failed \n");
        return res;
    }
    fclose(file);
    free(buf);
    return res;
}


uint32_t wav_buffill(uint8_t *buf,uint16_t size,uint8_t bits,__wavctrl *wavctrl)
{
	uint16_t readlen=0;
	uint32_t bread = 0;
	uint32_t i;
	uint8_t temp0=0,temp1=0;
	if(bits==24)//24bit音频,需要处理一下
	{
		if(wavctrl->nchannels==2)//双声道
		{
		  readlen=(size/4)*3;							//此次要读取的字节数
          bread = fread(buf,sizeof(char),readlen,g_file);
		  for(i=size;i>7;)
		  {
		     buf[i-1]=buf[i*3/4-3];
		     buf[i-2]=0;
		     buf[i-3]=buf[i*3/4-1];
		     buf[i-4]=buf[i*3/4-2];
			   i-=4;
		  }
		//开头四个数据处理
		    buf[3]=buf[0];
		    buf[0]=buf[1];
		    buf[1]=buf[2];
		    buf[2]=0;
		    bread=(bread/3)*4;		//填充后的大小.
		}
		else if(wavctrl->nchannels==1)//单声道
		{
			readlen=(size/8)*3;//此次要读取的字节数	//读取数据
            bread = fread(buf,sizeof(char),readlen,g_file);
        for(i=size/2;i>0;) 
		 {
		   buf[2*i-1]=buf[i*3/4-3];
		   buf[2*i-2]=0;
		   buf[2*i-3]=buf[i*3/4-1];
		   buf[2*i-4]=buf[i*3/4-2];
			 
			 buf[2*i-5]=buf[2*i-1];
			 buf[2*i-6]=buf[2*i-2];
			 buf[2*i-7]=buf[2*i-3];
			 buf[2*i-8]=buf[2*i-4];
			 i-=4;
		 }
		 bread=(bread/3)*8;		//填充后的大小.
		}	
	}
	else if(bits==16||bits==32)
	{
		if(wavctrl->nchannels==1)//单声道
		{
                //16bit音频,读取一半数据  
             bread = fread(buf,sizeof(char),size/2,g_file);
			 //将这一半数据进行自我复制为双声道数据
			if(wavctrl->bps==16)
			{
		   for(i=size;i>0;)
		   {
				  buf[i-3]=buf[i-1]=buf[i/2-1];
				  buf[i-4]=buf[i-2]=buf[i/2-2];
				  i-=4;
		   } 
		 }
			else if(wavctrl->bps==32)
			{
				for(i=size;i>0;)
				{
					buf[i-1]=buf[i/2-3];
					buf[i-2]=buf[i/2-4]; 
					buf[i-3]=buf[i/2-1]; 
					buf[i-4]=buf[i/2-2];
					buf[i-5]=buf[i-1];
					buf[i-6]=buf[i-2];
					buf[i-7]=buf[i-3];
					buf[i-8]=buf[i-4];
					i-=8;
				}
			}
			if(bread<size/2)//不够数据了,补充0
		  {
			  for(i=bread;i<size/2-bread;i++)buf[i]=0; 
		  }
			bread=2*bread;
		}
		else//双声道
		{
            bread = fread(buf,sizeof(char),size,g_file);
			//32bit音频,直接读取数据  
			if(bits==32)	//32位的时候,数据需要单独处理下
			{ 
				for(i=0;i<size;)
				{
					temp0=buf[i];
					temp1=buf[i+1];
					buf[i]=buf[i+2];
					buf[i+1]=buf[i+3]; 
					buf[i+2]=temp0; 
					buf[i+3]=temp1;
					i+=4;
				} 				
			}
			if(bread<size)//不够数据了,补充0
		  {
			  for(i=bread;i<size-bread;i++)buf[i]=0; 
		  }
	  }
	}
	else if(bits==8)
	{
		if(wavctrl->nchannels==1)//单声道
		{
            bread = fread(buf,sizeof(char),size/4,g_file);
			for(i=size;i>0;)
			{
				  buf[i-1]=buf[i/4-1]-0x80; 
				  buf[i-2]=0xff;
				  buf[i-3]=buf[i-1];
				  buf[i-4]=buf[i-2];
				  i-=4;
				 //buf[4*i+2]=buf[4*i]=0xff;
				// buf[4*i+3]=buf[4*i+1]=p[i]-0x80;
		  } 
			if(bread<size/4)//不够数据了,补充0
		  {
			  for(i=bread;i<size/4-bread;i++)buf[i]=0; 
		  }
			bread=4*bread;
		}
		else if(wavctrl->nchannels==2)//双声道
		{
            bread = fread(buf,sizeof(char),size/2,g_file);
			for(i=size;i>0;)
			{
				 buf[i-1]=buf[i/2-1]-0x80; 
				 buf[i-2]=0xff;
				i-=2;
		   } 
			if(bread<size/2)//不够数据了,补充0
		  {
			  for(i=bread;i<size/2-bread;i++)buf[i]=0; 
		  }
			bread=2*bread;
		}
	}
	return bread;
} 



//读文件，分析文件标识头
void wav_song_player(const char* const file_name)
{
    uint8_t res;
    if(!file_name){
        ESP_LOGI(TAG, "file name is null \n");
        return;
    }
    __wavctrl *wavctrl = malloc(sizeof(__wavctrl));

    res = wav_decode_init(file_name,wavctrl);
    if(res == 0){

        i2s_config_t i2s_config = {
            .mode = I2S_MODE_MASTER | I2S_MODE_TX,
            .sample_rate = wavctrl->samplerate,
            .bits_per_sample = wavctrl->bps,
            .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = 0, // default interrupt priority
            .dma_buf_count = 4,
            .dma_buf_len = 512,
            .use_apll = false
        };

		if(wavctrl->bps < 16){
			i2s_config.bits_per_sample = 16;
		}

        static const i2s_pin_config_t pin_config = {
            .bck_io_num = 16,
            .ws_io_num = 17,
            .data_out_num = 42,
            .data_in_num = I2S_PIN_NO_CHANGE
        };

        i2s_driver_install(i2s_num, &i2s_config, 0, NULL);   //install and start i2s driver

        // esp_intr_alloc(ETS_I2S0_INTR_SOURCE, ESP_INTR_FLAG_LOWMED | ESP_INTR_FLAG_IRAM, i2s_isr, NULL, NULL);

        i2s_set_pin(i2s_num, &pin_config);

		// i2s_zero_dma_buffer(i2s_num);

        // i2s_set_sample_rates(i2s_num, 22050); //set sample rates

        // i2s_driver_uninstall(i2s_num); //stop & destroy i2s driver


        size_t bytesWritten;
        uint32_t block_w = 1024;

		int availableBytes = 0;
		int buffer_position = 0;

        uint8_t *data_buf1 = malloc(block_w);
        ESP_LOGI(TAG, "paramter: samplerate:%d, bits:%d, bps:%d", wavctrl->samplerate, wavctrl->datasize, wavctrl->bps);

        g_file = fopen(file_name, "r");

        fseek(g_file,wavctrl->datastart,SEEK_SET);

		do{
			if(availableBytes == 0){
				// availableBytes = fread(data_buf1,sizeof(char),block_w,g_file);

				availableBytes = wav_buffill(data_buf1,block_w,wavctrl->bps,wavctrl);
				buffer_position = 0;
			}
			if(availableBytes > 0){
				i2s_write(i2s_num, buffer_position + data_buf1, availableBytes, &bytesWritten, 1000 / portTICK_PERIOD_MS);
				availableBytes -= bytesWritten;
				buffer_position += bytesWritten;
			}
		}while(bytesWritten > 0);

		ESP_LOGI(TAG, "play completed");
		vTaskDelay(500 / portTICK_PERIOD_MS);

        free(data_buf1);
    }
    fclose(g_file);
    // ESP_LOGI(TAG, "==============wav info===========\n");
    // ESP_LOGI(TAG, "===========data size:%d=======\n",wavctrl->datasize);
    // ESP_LOGI(TAG, "===========bitrate:%d=======\n",wavctrl->bitrate);
    // ESP_LOGI(TAG, "===========totsec:%d=======\n",wavctrl->datasize/(wavctrl->bitrate/8));
    // ESP_LOGI(TAG, "=================================\n");

    free(wavctrl);
}