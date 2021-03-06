/*
 * Car eye 车辆管理平台: www.car-eye.cn
 * Car eye 开源网址: https://github.com/Car-eye-team
 * MP4MuxerTest.cpp
 *
 * Author: Wgj
 * Date: 2019-03-04 22:37
 * Copyright 2019
 *
 * MP4混流器测试程序
 */

#include <iostream>
#include <windows.h>
#include "MP4Muxer.h"
#include "CEDebug.h"

// 包含音频
#define HAS_AUDIO

const char* TestH264 = "./test.264";
#ifdef HAS_AUDIO
const char* TestAAC = "./test.aac";
#endif

bool threadIsWork = false;

/*
* Comments: 指定地址数据是否为H264帧起始标识
* Param aBytes: 要检测的内存地址
* @Return 是否为H264起始标识
*/
bool IsH264Flag(uint8_t *aBytes)
{
	if (aBytes[0] == 0x00 && aBytes[1] == 0x00
		&& aBytes[2] == 0x00 && aBytes[3] == 0x01)
	{
		uint8_t nal_type = aBytes[4] & 0x1F;
		return (nal_type == 0x07 || nal_type == 0x01);
	}

	return false;
}

/*
* Comments: 读取一帧H264数据
* Param aFile: 要读取的文件句柄
* Param aBytes: 读取到的帧数据
* @Return 大于0为读取到的帧数据个数, 否则失败
*/
int ReadH264Frame(FILE *aFile, uint8_t *aBytes)
{
	uint8_t *bytes = aBytes;
	int read_cnt = 0;
	bool is_head = false;
	int result = 0;

	while (true)
	{
		// 查找帧开头
		if ((result = fread(bytes, 1, 1, aFile)) < 1)
		{
//			DEBUG_W("Read fail: %d, %d.", result, feof(aFile));
			// 读完了
			return bytes - aBytes;
		}

		read_cnt++;
		bytes++;

		if (read_cnt >= 5)
		{
			if (IsH264Flag(bytes - 5))
			{
				if (!is_head)
				{
					is_head = true;
					memcpy(aBytes, bytes - 5, 5);
					bytes = aBytes + 5;
					read_cnt = 0;
				}
				else
				{
					bytes -= 5;
					// 前移5个字节
					fseek(aFile, -5, SEEK_CUR);
					return bytes - aBytes;
				}
			}
		}
	}
}

int main()
{
	AVFormatContext* fmt_h264 = NULL;
	AVFormatContext* fmt_aac = NULL;
	int result = 0;
	bool video_finished = false, audio_finished = true;
	FILE *pFile = nullptr;
	uint8_t *read_pkt = (uint8_t *)malloc(50 * 1024);

#ifdef HAS_AUDIO
	AVPacket pkt_v;
	audio_finished = false;
#endif

    std::cout << "Start muxer..." << std::endl;

	av_register_all();

	if ((result = avformat_open_input(&fmt_h264, TestH264, 0, 0)) < 0)
	{
		DEBUG_E("Could not open input file: -%08X.", -result);
		goto end;
	}
	if ((result = avformat_find_stream_info(fmt_h264, 0)) < 0)
	{
		DEBUG_E("Failed to retrieve input stream information -%08X.", -result);
		goto end;
	}

#ifdef HAS_AUDIO
	if ((result = avformat_open_input(&fmt_aac, TestAAC, 0, 0)) < 0)
	{
		DEBUG_E("Could not open input file: -%08X.", -result);
		goto end;
	}
	if ((result = avformat_find_stream_info(fmt_aac, 0)) < 0)
	{
		DEBUG_E("Failed to retrieve input stream information -%08X.", -result);
		goto end;
	}
#endif

	// 输出文件信息
	printf("av_dump_format() h264输出:\n");
	av_dump_format(fmt_h264, 0, TestH264, 0);

#ifdef HAS_AUDIO
	printf("av_dump_format aac输出:\n");
	av_dump_format(fmt_aac, 0, TestAAC, 0);
#endif
	avformat_close_input(&fmt_h264);

	if (fopen_s(&pFile, TestH264, "rb") != 0)
	{
		DEBUG_E("Open %s failed.", TestH264);
		goto end;
	}

	// 实例化混流器
	MP4Muxer *muxer = new MP4Muxer();
	if (!muxer->Start("./test.mp4"))
	{
		delete muxer;
		DEBUG_E("Muxer start fail.");
		goto end;
	}

	while (1)
	{
		// 读取一帧视频数据
		if (!video_finished)
		{
			//			if ((result = av_read_frame(fmt_h264, &pkt_v)) < 0)
			if ((result = ReadH264Frame(pFile, read_pkt)) <= 0)
			{
				DEBUG_W("Read frame fail: -%08X.", -result);
				video_finished = true;
				if (audio_finished)
				{
					break;
				}
				continue;
			}
			// 需要确保传入的是一帧数据
// 			DEBUG_V("Read size: %d, %02X %02X %02X %02X %02X %02X %02X %02X.",
// 					result, read_pkt[0], read_pkt[1], read_pkt[2], read_pkt[3], 
// 					read_pkt[4], read_pkt[5], read_pkt[6], read_pkt[7]);
			muxer->AppendVideo(read_pkt, result);
		}

#ifdef HAS_AUDIO
		// 读取一帧音频数据
		if (!audio_finished)
		{
			if ((result = av_read_frame(fmt_aac, &pkt_v)) < 0)
			{
				DEBUG_W("Read frame fail: -%08X.", -result);
				audio_finished = true;
				if (video_finished)
				{
					break;
				}
				continue;
			}
			// 需要确保传入的是一帧数据
			muxer->AppendAudio(pkt_v.data, pkt_v.size);
		}
#endif
	}

	fclose(pFile);
	free(read_pkt);

#ifdef HAS_AUDIO
	av_free_packet(&pkt_v);
	avformat_close_input(&fmt_aac);
#endif
	muxer->Stop();
	delete muxer;

end:
	_DEBUG_I("Muxer finished.\n");
	getchar();
}
