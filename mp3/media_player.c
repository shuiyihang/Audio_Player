/**
 * 初始化播放列表
 * 
 * 双击上一曲，三击下一曲功能
 * 
 * 单击播放暂停功能
 * 
 * 旋转加减音量
*/
#include "media_player.h"
#include "driver/i2s.h"


extern media_play_ctl_t* play_ctl;


play_list_t song_list;

u8_t __match_file_type(const char* url, const char* suffix)
{
	u8_t url_len = strlen(url);
	u8_t suffix_len = strlen(suffix);
	//全部转为大写比较
	#define TO_UPPER(ch)	(((ch) >= 'a' && (ch) <= 'z') ? ((ch)-('a' - 'A')) : (ch))
	int i;
	for(i = 0; i < suffix_len; i++){
		if(TO_UPPER(url[url_len-suffix_len+i]) != TO_UPPER(suffix[i])){
			break;
		}
	}
	
	if(i == suffix_len){
		return 1;
	}
	return 0;
}

media_type_t get_music_type(const char* url)
{
	if(__match_file_type(url,"wav")){
		return WAV_TYPE;
	}

	if(__match_file_type(url,"mp3")){
		return MP3_TYPE;
	}

	return UNSUPPORT_TYPE;
}






int __iterator_data_init(iterator_data_t* data, const void* param)
{
	int res = 0;

	char* path = (char*)(param);

	for(int i = 0;i < MAX_DIR_LEVEL;i++){
		data->dirs[i] = (FF_DIR*)malloc(sizeof(FF_DIR));
		if(!data->dirs[i]){
			res = 1;
			printf("malloc failed \n");
			break;
		}
	}

	for(int i = 0; i < SUPPORT_FOLDER_CNT; i++) {
		song_list.folder_info[i] = malloc(sizeof(folder_info_t));
	}

	data->full_path = (char*)malloc(sizeof(char) * FULL_PATH_LEN);
	if(!data->full_path){
		res = 1;
	}
	memset(data->full_path,0,FULL_PATH_LEN);
	
	data->dirent = (fs_dirent_t*)malloc(sizeof(fs_dirent_t));

	data->dname_len = malloc(sizeof(*data->dname_len) * MAX_DIR_LEVEL);

	strcpy(data->full_path,path);
	data->full_len = strlen(path);
	data->fname_len = 0;
	data->level = 0;

	if(data->full_path[data->full_len - 1] != '/'){//传进来的路径是/sdcard的时候，手动补为/sdcard/
		data->full_path[data->full_len++] = '/';
	}
	data->dname_len[0] = data->full_len;

	return res;
}


int fs_readdir(FF_DIR* dp, fs_dirent_t* entry)
{
	u8_t res;
	FILINFO *fno = malloc(sizeof(FILINFO));
	if (!fno)
		return -1;

	res = f_readdir(dp, fno);
	if (res == FR_OK) {
		entry->type = ((fno->fattrib & AM_DIR) ?
			       FS_DIR_ENTRY_DIR : FS_DIR_ENTRY_FILE);
		strcpy(entry->name, fno->fname);
		entry->size = fno->fsize;
	}

	free(fno);

	return res;
}

void playlist_folder_info_set(play_list_t* play_list,FF_DIR* dp,u8_t seq_num,u8_t layer)
{
	play_list->folder_info[seq_num]->cluster = dp->clust;
	play_list->folder_info[seq_num]->file_count = 0;//当前文件夹下文件数初始化为0
	play_list->folder_info[seq_num]->layer = layer;
}

void __scan_disk_playfile(play_list_t* play_list, iterator_data_t* data)
{
	u8_t is_find_file = 1;
	u8_t exist_sub_folder = 0;

	u16_t name_len = 0;
	int res = 0;
	do
	{
		res = fs_readdir(data->dirs[data->level], data->dirent);
		if(res || data->dirent->name[0] == 0){
			//一个文件夹访问完全
			f_closedir(data->dirs[data->level]);
			if(data->level == 0 && (is_find_file == 0 || exist_sub_folder == 0)){
				//顶层目录，子文件夹访问完全 或 不存在子文件夹，可以返回了
				break;
			}
			if(is_find_file && exist_sub_folder){//重新打开一遍目录，目标改为文件夹
				is_find_file = 0;
				exist_sub_folder = 0;
				if(data->full_path[data->full_len - 1] == '/'){//有没有必要呢???
					data->full_path[data->full_len - 1] = '\0';//打开/sdcard
				}
				printf("reopen dir,name:%s \n",data->full_path);
				f_opendir(data->dirs[data->level] , data->full_path);
				if(data->full_path[data->full_len - 1] == '\0'){//有没有必要呢???
					data->full_path[data->full_len - 1] = '/';//打开/sdcard
				}
				continue;
			}

			//不是顶层目录，当前文件夹下文件和子目录全访问完，返回上一级
			
			data->full_len -= (data->fname_len + data->dname_len[data->level]);
			data->full_path[data->full_len] = '\0';

			data->fname_len = 0;

			data->level -- ;
			is_find_file = 0;//接着寻找剩下的文件夹
			continue;
		}

		if(data->dirent->name[0] == '.'){
			continue;//忽略隐藏文件
		}

		if((data->dirent->type == FS_DIR_ENTRY_FILE) && (get_music_type(data->dirent->name) == UNSUPPORT_TYPE)){
			continue;//过滤掉不支持的文件格式
		}

		if(is_find_file && data->dirent->type == FS_DIR_ENTRY_FILE){
			play_list->folder_info[play_list->sum_folder_count]->file_count++;//该文件夹下文件数+1
			play_list->sum_file_count++;
			printf("this is a file,name:%s \n",data->dirent->name);
			continue;
		}
		
		//寻找目标变为文件夹时候，不再计算合法文件
		if(is_find_file == 0 && data->dirent->type == FS_DIR_ENTRY_FILE){
			continue;
		}

		if(is_find_file == 1 && data->dirent->type == FS_DIR_ENTRY_DIR){
			exist_sub_folder = 1;
			continue;
		}

		name_len = strlen(data->dirent->name);

		strcpy(&data->full_path[data->full_len],data->dirent->name);
		data->full_len += name_len;
		//开始搜索文件夹
		printf("enter dir,name:%s \n",data->full_path);
		f_opendir(data->dirs[data->level + 1],data->full_path);//打开的是"/sdcard/dir1"

		play_list->sum_folder_count++;

		//拿到新打开的文件夹所在的簇
		playlist_folder_info_set(play_list,data->dirs[data->level + 1],play_list->sum_folder_count,data->level + 1);
		
		is_find_file = 1;//进入新文件夹，首要找文件

		data->full_path[data->full_len] = '/';//添加分隔符
		data->full_path[++data->full_len] = '\0';//添加字符串结束

		data->dname_len[++data->level] = name_len + 1;//标注的是"dir1/"长

		data->fname_len = 0;


	} while (1);
	
}



void calc_palylist_curfile_info(play_list_t* play_list)
{
	int i = 0;
	u32_t sum_files = 0;
	//查找到文件在第几个文件夹下，在该文件夹下序号
	for(; i <= play_list->sum_folder_count; i++){
		sum_files += play_list->folder_info[i]->file_count;
		if(play_list->file_seq_num <= sum_files){
			play_list->dir_file_seq_num = play_list->file_seq_num - (sum_files - play_list->folder_info[i]->file_count);
			break;
		}
	}
	//
	if(i <= play_list->sum_folder_count){
		play_list->folder_seq_num = i;
	}
	printf("dir file seq:%d,folder_seq:%d \n",play_list->dir_file_seq_num,i);
}

//以簇的方式打开文件夹，得到目标文件的簇和偏移
void get_clust_from_dirseq(play_list_t* play_list , u32_t* cluster, DWORD* blk_ofs)
{
	int res = 0;
	u16_t valid_files = 0;
	FF_DIR* dp = (FF_DIR*)malloc(sizeof(FF_DIR));
	fs_dirent_t *entry = malloc(sizeof(fs_dirent_t));

	res = f_opendir_cluster(dp,play_list->topdir,play_list->folder_info[play_list->folder_seq_num]->cluster,0);
	if(!res){
		//成功打开文件夹
		do
		{
			memset(entry, 0, sizeof(fs_dirent_t));
			res = fs_readdir(dp,entry);
			if(res || entry->name[0] == 0){
				//文件夹结束
				printf("dir end error! \n");
				break;
			}
			if((entry->type == FS_DIR_ENTRY_FILE) && (get_music_type(entry->name) != UNSUPPORT_TYPE)){
				valid_files++;
			}
		} while (valid_files < play_list->dir_file_seq_num);

		f_closedir(dp);

		*cluster = play_list->folder_info[play_list->folder_seq_num]->cluster;
		*blk_ofs = dp->blk_ofs;

		printf("cluster:%u,blk:%zu \n",*cluster,*blk_ofs);
	}else{
		printf("fopen dir failed \n");
	}
	free(dp);
	free(entry);
}


//由文件的序号，得到文件所在簇，偏移。然后打开此文件，获得数据
void seqnum_to_filelocal(play_list_t* play_list,u32_t* cluster, DWORD* blk_ofs)
{
	calc_palylist_curfile_info(play_list);

	get_clust_from_dirseq(play_list,cluster,blk_ofs);
}




void media_playlist_init(const char* top_dir)//"/sdcard 或 /sdcard/"
{
	iterator_data_t iter;

	FRESULT res;
	
	song_list.topdir = top_dir;

	__iterator_data_init(&iter, top_dir);

    //保存文件簇，获得文件名
	if(iter.full_path[iter.full_len - 1] == '/'){//很有必要,否则无法打开/sdcard/
		iter.full_path[iter.full_len - 1] = '\0';//打开/sdcard
	}
	res = f_opendir(iter.dirs[0] , iter.full_path);

	if(res){
		printf("opendir is failed,path:%s \n",iter.full_path);
		return;
	}

	if(iter.full_path[iter.full_len - 1] == '\0'){
		iter.full_path[iter.full_len - 1] = '/';//恢复为/sdcard/
	}

	if(!iter.dirs[0]){
		printf("iter dirs[0] is null \n");
		return;
	}

	song_list.sum_folder_count = 0;
	song_list.folder_info[0]->cluster = iter.dirs[0]->clust;
	song_list.folder_info[0]->layer = 0;
	song_list.folder_info[0]->file_count = 0;

	__scan_disk_playfile(&song_list,&iter);

	for(int i = 0;i <= song_list.sum_folder_count;i++){
		printf("folder[%d] has file num:%d \n",i,song_list.folder_info[i]->file_count);
	}

	song_list.file_seq_num = 0;//我想获得第x个mp3文件

	u32_t cluster;
	DWORD blk_ofs;
	FIL* fp = (FIL*)malloc(sizeof(FIL));


	

	//做一个文件打开的测试
	do{
		printf("select file %d \n",song_list.file_seq_num);
		seqnum_to_filelocal(&song_list,&cluster,&blk_ofs);
		res = f_open_cluster(fp,song_list.topdir,cluster,blk_ofs,FA_READ | FA_OPEN_EXISTING);
		if(!res){
			madlld_song_player(fp);
		}else{
			song_list.file_seq_num++;
			if(song_list.file_seq_num > song_list.sum_file_count){
				song_list.file_seq_num = 0;
			}
		}
		if(play_ctl->is_exit){
			break;
		}
		// i2s_stop(0);
	}while(1);

	f_close(fp);

}



//根据编码器消息，可以关闭当前音乐打开新的音乐播放

void next_song(void)
{
	song_list.file_seq_num++;
	if(song_list.file_seq_num > song_list.sum_file_count){
		song_list.file_seq_num = 0;//从第一个文件开始播放
	}
}