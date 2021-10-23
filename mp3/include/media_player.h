#pragma once
#include "string.h"
#include "ff.h"
#include "madlld.h"


#define SUPPORT_FOLDER_CNT  50
#define MAX_DIR_LEVEL		5	//向下迭代的最大深度

#define FULL_PATH_LEN		128

#define MAX_FILE_NAME		32

typedef unsigned int 	u32_t;
typedef unsigned short 	u16_t;
typedef unsigned char  	u8_t;


enum fs_dir_entry_type {
	FS_DIR_ENTRY_FILE,
	FS_DIR_ENTRY_DIR
};

typedef struct{
	enum fs_dir_entry_type type;//文件类型
	char name[MAX_FILE_NAME + 1];//文件名
	size_t size;
}fs_dirent_t;


typedef struct{
    u32_t cluster;		/*当前文件夹所在簇*/
	u16_t file_count;	/*文件夹下支持播放的音频文件*/
	u8_t layer;         /*文件夹所在深度*/
}folder_info_t;

typedef struct{
    folder_info_t *folder_info[SUPPORT_FOLDER_CNT];
    u16_t sum_file_count;	/*sd卡中总的支持播放的音频文件个数*/
	u16_t file_seq_num;		/*音频文件在播放列表中序号*/
	u8_t sum_folder_count;	/*sd卡中总的文件夹个数*/
	u8_t folder_seq_num;	/*当前音频文件所在文件夹的序号*/
	u16_t dir_file_seq_num;	/*当前音频文件在文件夹中的序号*/
	u8_t mode;				/*play mode 0--Full cycle;1--single cycle;2--full,no cycle;3--folder cycle*/
	const char *topdir;
}play_list_t;


typedef struct{
	char* full_path;//完整路径
	FF_DIR* dirs[MAX_DIR_LEVEL];

	u16_t *dname_len;
	u16_t fname_len;
	u16_t full_len;

	u16_t level;

	fs_dirent_t* dirent;
	
}iterator_data_t;

typedef enum{
	UNSUPPORT_TYPE = 0,

	WAV_TYPE,

	MP3_TYPE,

}media_type_t;



//暂时存放

typedef struct 
{
	u8_t is_switch:1;//上一曲，下一曲，需要退出当前播放重新打开fp
	u8_t is_pause:1;// 0 playing , 1 pause
    u8_t is_exit:1;
}media_play_ctl_t;




void next_song(void);