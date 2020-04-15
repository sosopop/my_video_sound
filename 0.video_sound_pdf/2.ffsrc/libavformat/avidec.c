/*
AVI�ļ���������غ�����ע����Щ�ط���Щ�����Դ��롣

ע��1��AVI �ļ�����ý�����������ִ�ŷ�ʽ���ǽ�֯��źͽ�֯��š�
��֯���:
��������Ƶ������֡Ϊ��С������λ��
�໥�����ţ���������Ƶ֡���ཻ֯��һ��
���Ҵ�ŵļ��û���ر�涨��

�ǽ�֯���:
���ǰѵ�һý�����������֡���������һ�𣬷ǽ�֯��ŵ�avi �ļ����١�

ע��2�� AVI �ļ������ṹAVIINDEXENTRY �е�dwChunkOffset �ֶ�ָʾ��ƫ���е�������ļ���ʼ�ֽڵ�ƫ
�ƣ��е�������ļ����ݿ�chunk ��ƫ�ơ�

ע��3�������� avi�����ļ��ǽ�֯��ŵ�
*/

#include "avformat.h"

#include <assert.h>

#define AVIIF_INDEX			0x10

#define AVIF_HASINDEX		0x00000010	// Index at end of file?
#define AVIF_MUSTUSEINDEX	0x00000020

#define INT_MAX	2147483647

#define MKTAG(a,b,c,d) (a | (b << 8) | (c << 16) | (d << 24))

#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
#define FFMAX(a,b) ((a) > (b) ? (a) : (b))

static int avi_load_index(AVFormatContext *s);
static int guess_ni_flag(AVFormatContext *s);

//AVI �ļ��е����������壬��AVStream ���ݽṹЭ����
typedef struct
{
	// ֡ƫ�ƣ���Ƶ��֡��������Ƶ���ֽڼ��������ڼ���pts ��ʾʱ��
	int64_t frame_offset; // current frame(video) or byte(audio) counter(used to compute the pts)
	// ��ʾ��Ҫ�������ݴ�С����ֵ��֡�������С��ȫ�������Ϊ0��
	int remaining;
	// ����С���ǽ�֯��֡�����ݴ�С��ͬ����֯��֡�����ݴ�С��8 �ֽ�
	int packet_size;

	int scale;
	int rate;
	int sample_size; // size of one sample (or packet) (in the rate/scale sense) in bytes

	int64_t cum_len; // temporary storage (used during seek)

	int prefix;      // normally 'd'<<8 + 'c' or 'w'<<8 + 'b'
	int prefix_count;
} AVIStream;

//AVI�ļ��е��ļ���ʽ������ض��壬��AVFormatContext Э����
typedef struct
{
	int64_t riff_end;    // RIFF���С
	int64_t movi_list;   // ý�����ݿ鿪ʼ�ֽ�����ļ���ʼ�ֽڵ�ƫ��
	int64_t movi_end;    // ý�����ݿ鿪ʼ�ֽ�����ļ���ʼ�ֽڵ�ƫ��
	int non_interleaved; // ָʾ�Ƿ��Ƿǽ�֯AVI
	// Ϊ�˺�AVPacket �е�stream_index �������һ����׺��
	// ָʾ��ǰӦ�ö�ȡ��������������ֵΪ-1����ʾû��ȷ��Ӧ�ö�������
	// ʵ�ʱ�ʾAVFormatContext �ṹ��AVStream*streams[]�����е�������}
	int stream_index_2;  // Ϊ�˺�AVPacket�е�stream_index������
} AVIContext;

//CodecTag���ݽṹ�����ڹ�������ý���ʽ��ID��Tag ��ǩ��
typedef struct
{
	int id;
	unsigned int tag;
} CodecTag;

//�������ffplay֧�ֵ�һЩ��Ƶý��ID��Tag ��ǩ���顣
const CodecTag codec_bmp_tags[] =
{
	{CODEC_ID_MSRLE, MKTAG('m', 'r', 'l', 'e')},
	{CODEC_ID_MSRLE, MKTAG(0x1, 0x0, 0x0, 0x0)},
	{CODEC_ID_NONE,  0},
};

//�������ffplay֧�ֵ�һЩ��Ƶý��ID��Tag ��ǩ���顣
const CodecTag codec_wav_tags[] =
{
	{CODEC_ID_TRUESPEECH, 0x22},
	{0, 0},
};

//��ý��tag��ǩΪ�ؼ��֣����� codec_bmp_tags�� codec_wav_tags���飬����ý��ID��
enum CodecID codec_get_id(const CodecTag *tags, unsigned int tag)
{
	while (tags->id != CODEC_ID_NONE)
	{
		//�Ƚ�Tag�ؼ��֣����ʱ���ض�Ӧý��ID��
		if (toupper((tag >> 0) &0xFF) == toupper((tags->tag >> 0) &0xFF)
				&& toupper((tag >> 8) &0xFF) == toupper((tags->tag >> 8) &0xFF)
				&& toupper((tag >> 16)&0xFF) == toupper((tags->tag >> 16)&0xFF)
				&& toupper((tag >> 24)&0xFF) == toupper((tags->tag >> 24)&0xFF))
			return (enum CodecID)tags->id;

		tags++;
	}
	//���йؼ��ֶ���ƥ�䣬���� CODEC_ID_NONE��
	return CODEC_ID_NONE;
}

//У��AVI�ļ�����ȡAVI�ļ�ý�����ݿ��ƫ�ƴ�С��Ϣ�� ��avi_probe()����������ͬ��
static int get_riff(AVIContext *avi, ByteIOContext *pb)
{
	//printf("get_riff()\n");
	uint32_t tag;
	tag = get_le32(pb);

	//У�� AVI�ļ���ʼ�ؼ��ִ�"RIFF"��
	if (tag != MKTAG('R', 'I', 'F', 'F'))
		return  - 1;

	avi->riff_end = get_le32(pb); // RIFF chunk size
	//printf("avi->riff_end:%d\n", avi->riff_end);//64360
	avi->riff_end += url_ftell(pb); // RIFF chunk end
	//printf("avi->riff_end:%d\n", avi->riff_end);//64368
	tag = get_le32(pb);

	//У�� AVI�ļ��ؼ��ִ�"AVI "��"AVIX"��
	if (tag != MKTAG('A', 'V', 'I', ' ') && tag != MKTAG('A', 'V', 'I', 'X'))
		return  - 1;

	//���ͨ�� AVI�ļ��ؼ��ִ�"RIFF"��"AVI "��"AVIX"У�飬����Ϊ�� AVI�ļ������ַ�ʽ�ǳ��ɿ���
	return 0;
}

//������AVI��������������Ϊclean_index,��׼ȷ�������Ծ����ʵ�ִ���Ϊ׼��
static void clean_index(AVFormatContext *s)
{
	int i, j;

	for (i = 0; i < s->nb_streams; i++)
	{
		//��ÿ��������һ����������������
		AVStream *st = s->streams[i];
		AVIStream *ast = (AVIStream *)st->priv_data;
		int n = st->nb_index_entries;
		int max = ast->sample_size;
		int64_t pos, size, ts;

		//��������������1������Ϊ�������ѽ��ã����������ؽ������sample_size Ϊ0,��û�취�ؽ���
		if (n != 1 || ast->sample_size == 0)
			continue;

		//���������������ڷǽ�֯�洢��avi��Ƶ�������ܽ�֯���Ƿǽ�֯�洢����Ƶ��ͨ������������
		//��ֹ��̫С��Ҫ̫���������ռ�д����ڴ棬�趨��С֡size ��ֵΪ1024��������Щ��Ƶ������С��
		//��ֻ֡ʮ����ֽڣ� ����ļ��Ƚϴ����������Ϻķ�̫���ڴ�
		while (max < 1024)
			max += max;

		//ȡλ�ã���С��ʱ�ӵȻ���������
		pos = st->index_entries[0].pos;
		size = st->index_entries[0].size;
		ts = st->index_entries[0].timestamp;

		//��maxָ�����ֽڴ����֡�����ӵ���������
		for (j = 0; j < size; j += max)
		{
			av_add_index_entry(st, pos + j, ts + j / ast->sample_size, FFMIN(max, size - j), 0, AVINDEX_KEYFRAME);
		}
	}
}//end clean_index()

/*
 ./ffplay
	av_open_input_file()
	url_open()
	proto_str:file
	up->name
	avo_read_header
	size:1360
	LIST
	size:56
	avih
	size:1140
	LIST
	size:56
	strh
	size:1064
	strf
	size:126
	LIST
	size:56
	strh
	size:50
	strf
	size:2
	url_fskip size:2
	size:62468
	LIST
	movi
	avi_load_index()
*/
//��ȡAVI�ļ�ͷ����ȡAVI�ļ���������ʶ������ý���ʽ������һЩ���ݽṹ��
static int avi_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
	printf("avi_read_header()\n");
	AVIContext *avi = (AVIContext *)s->priv_data;
	ByteIOContext *pb = &s->pb;
	uint32_t tag, tag1, handler;
	int codec_type, stream_index, frame_period, bit_rate;
	unsigned int size, nb_frames;
	int i, n;
	AVStream *st;
	AVIStream *ast;

	//��ǰӦ�ö�ȡ��������������ֵΪ-1����ʾû��ȷ��Ӧ�ö�������
	avi->stream_index_2 =  - 1;

	//У��AVI�ļ�����ȡAVI�ļ�ý�����ݿ��ƫ�ƴ�С��Ϣ��
	if (get_riff(avi, pb) < 0)
		return  - 1;

	stream_index =  - 1; // first list tag
	codec_type =  - 1;
	frame_period = 0;

	//AVI �ļ��Ļ����ṹ�ǿ飬һ���ļ��ж���飬���ҿ黹������Ƕ��������ѭ�����ļ�ͷ�еĿ顣
	for (;;)
	{
		if (url_feof(pb))
			goto fail;

		//��ȡÿ����ı�ǩ�ʹ�С��
		tag = get_le32(pb);
		size = get_le32(pb);
		printf("size:%d\n", size);
		switch (tag)
		{
			case MKTAG('L', 'I', 'S', 'T'):  // ignored, except when start of video packets
				printf("LIST\n");
				tag1 = get_le32(pb);
				if (tag1 == MKTAG('m', 'o', 'v', 'i'))
				{
					printf("movi\n");
					//��ȡmoviý�����ݿ��ƫ�ƺʹ�С��
					avi->movi_list = url_ftell(pb) - 4;
					if (size)
						avi->movi_end = avi->movi_list + size;
					else
						avi->movi_end = url_fsize(pb);
					//AVI �ļ�ͷ������movi ý�����ݿ飬���Ե���movi�飬�ļ�ͷ�϶����꣬��Ҫ����ѭ����
					goto end_of_header; // �������ݶξ���Ϊ�ļ�ͷ�����ˣ���goto
				}
				break;
			case MKTAG('a', 'v', 'i', 'h'):  // avi header, using frame_period is bad idea
				printf("avih\n");
				frame_period = get_le32(pb);
				bit_rate = get_le32(pb) *8;
				get_le32(pb);
				//��ȡ non_interleaved�ĳ�ֵ��
				avi->non_interleaved |= get_le32(pb) &AVIF_MUSTUSEINDEX;

				url_fskip(pb, 2 *4);
				n = get_le32(pb);
				for (i = 0; i < n; i++)
				{
					//��ȡ����Ŀn�󣬷��� AVStream �� AVIStream���ݽṹ���� st->priv_data = ast;�а����ǹ���������
					//�ر�ע�� av_new_stream()��������AVFormatContext ��AVStream �ṹ�� �������AVCodecContext �ṹ
					AVIStream *ast;
					st = av_new_stream(s, i);
					if (!st)
						goto fail;

					ast = (AVIStream *)av_mallocz(sizeof(AVIStream));
					if (!ast)
						goto fail;
					st->priv_data = ast;

					st->actx->bit_rate = bit_rate;
				}
				url_fskip(pb, size - 7 * 4);
				break;
			case MKTAG('s', 't', 'r', 'h'):  // stream header
				printf("strh\n");
				//ָʾ��ǰ���� AVFormatContext�ṹ�� AVStream*streams[MAX_STREAMS]�����е�������
				stream_index++;
				//�� strh���ȡ���������е�һЩ��Ϣ��������Щ���õ��ֶΣ���д��Ҫ���ֶΡ�
				tag1 = get_le32(pb);
				handler = get_le32(pb);

				//�����������ͨ������ý���ļ������д��� ffplay �򵥵�������
				if (stream_index >= s->nb_streams)
				{
					url_fskip(pb, size - 8);
					break;
				}
				st = s->streams[stream_index];
				ast = (AVIStream *)st->priv_data;

				get_le32(pb); // flags
				get_le16(pb); // priority
				get_le16(pb); // language
				get_le32(pb); // initial frame
				ast->scale = get_le32(pb);
				ast->rate = get_le32(pb);
				if (ast->scale && ast->rate)
				{}
				else if (frame_period)
				{
					ast->rate = 1000000;
					ast->scale = frame_period;
				}
				else
				{
					ast->rate = 25;
					ast->scale = 1;
				}
				//���õ�ǰ����ʱ����Ϣ�����ڼ��� pts��ʾʱ�䣬����ͬ����
				av_set_pts_info(st, 64, ast->scale, ast->rate);

				ast->cum_len = get_le32(pb); // start
				nb_frames = get_le32(pb);

				get_le32(pb); // buffer size
				get_le32(pb); // quality
				ast->sample_size = get_le32(pb); // sample ssize

				switch (tag1)
				{
					case MKTAG('v', 'i', 'd', 's'):
						//�ر�ע����Ƶ����ÿһ֡��С��ͬ������ sample_size ����Ϊ 0��
						//�Ա���Ƶ��ÿһ֡��С�̶��������
						codec_type = CODEC_TYPE_VIDEO;
						ast->sample_size = 0;
						break;
					case MKTAG('a', 'u', 'd', 's'):
						codec_type = CODEC_TYPE_AUDIO;
						break;
					case MKTAG('t', 'x', 't', 's'):
						//FIXME
						codec_type = CODEC_TYPE_DATA; //CODEC_TYPE_SUB ?  FIXME
						break;
					case MKTAG('p', 'a', 'd', 's'):
						//������������ stream_index ��1 ��ʵ���˼򵥵Ķ�����
						//����������Ŀ������
						codec_type = CODEC_TYPE_UNKNOWN;
						stream_index--;
						break;
					default:
						goto fail;
				}
				ast->frame_offset = ast->cum_len *FFMAX(ast->sample_size, 1);
				url_fskip(pb, size - 12 * 4);
				break;
			case MKTAG('s', 't', 'r', 'f'):  // stream header
				printf("strf\n");
				//��strf���ȡ���б��������һЩ��Ϣ��������Щ���õ��ֶΣ���д��Ҫ���ֶΡ�
				//ע����Щ���������Ҫ�ĸ�����Ϣ�Ӵ˿��ж����������� extradata�����մ�����Ӧ�ı������
				if (stream_index >= s->nb_streams)
				{
					url_fskip(pb, size);
				}
				else
				{
					st = s->streams[stream_index];
					switch (codec_type)
					{
						case CODEC_TYPE_VIDEO:    // BITMAPINFOHEADER
							get_le32(pb); // size
							st->actx->width = get_le32(pb);
							st->actx->height = get_le32(pb);
							get_le16(pb); // panes
							st->actx->bits_per_sample = get_le16(pb); // depth
							tag1 = get_le32(pb);
							get_le32(pb); // ImageSize
							get_le32(pb); // XPelsPerMeter
							get_le32(pb); // YPelsPerMeter
							get_le32(pb); // ClrUsed
							get_le32(pb); // ClrImportant

							if (size > 10 *4 && size < (1 << 30))
							{
								//����Ƶ�� extradata ͨ���Ǳ������BITMAPINFO
								st->actx->extradata_size = size - 10 * 4;
								st->actx->extradata = (unsigned char*) av_malloc(st->actx->extradata_size +
										FF_INPUT_BUFFER_PADDING_SIZE);
								url_fread(pb, st->actx->extradata, st->actx->extradata_size);
							}

							if (st->actx->extradata_size &1)
								get_byte(pb);
							/* Extract palette from extradata if bpp <= 8 */
							/* This code assumes that extradata contains only palette */
							/* This is true for all paletted codecs implemented in ffmpeg */
							if (st->actx->extradata_size && (st->actx->bits_per_sample <= 8))
							{
								int min = FFMIN(st->actx->extradata_size, AVPALETTE_SIZE);
								st->actx->palctrl = (AVPaletteControl*) av_mallocz(sizeof(AVPaletteControl));
								memcpy(st->actx->palctrl->palette, st->actx->extradata, min);
								st->actx->palctrl->palette_changed = 1;
							}
							st->actx->codec_type = CODEC_TYPE_VIDEO;
							st->actx->codec_id = codec_get_id(codec_bmp_tags, tag1);
							st->frame_last_delay = 1.0 * ast->scale / ast->rate;
							break;
						case CODEC_TYPE_AUDIO:
							{
								AVCodecContext *actx = st->actx;
								int id = get_le16(pb);
								actx->codec_type = CODEC_TYPE_AUDIO;
								actx->channels = get_le16(pb);
								actx->sample_rate = get_le32(pb);
								actx->bit_rate = get_le32(pb) *8;
								actx->block_align = get_le16(pb);
								if (size == 14)  // We're dealing with plain vanilla WAVEFORMAT
									actx->bits_per_sample = 8;
								else
									actx->bits_per_sample = get_le16(pb);

								// wav_codec_get_id(id, codec->bits_per_sample);
								actx->codec_id = codec_get_id(codec_wav_tags, id);

								if (size > 16)
								{
									actx->extradata_size = get_le16(pb); // We're obviously dealing with WAVEFORMATEX
									if (actx->extradata_size > 0)
									{
										//����Ƶ�� extradata ͨ���Ǳ������WAVEFORMATEX
										if (actx->extradata_size > size - 18)
											actx->extradata_size = size - 18;
										actx->extradata = (unsigned char*)av_mallocz(actx->extradata_size +
												FF_INPUT_BUFFER_PADDING_SIZE);
										url_fread(pb, actx->extradata, actx->extradata_size);
									}
									else
									{
										actx->extradata_size = 0;
									}
									// It is possible for the chunk to contain garbage at the end
									if (size - actx->extradata_size - 18 > 0)
										url_fskip(pb, size - actx->extradata_size - 18);
								}
							}

							if (size % 2) // 2-aligned (fix for Stargate SG-1 - 3x18 - Shades of Grey.avi)
								url_fskip(pb, 1);

							break;
						default:
							//�����������ͣ� ffplay �򵥵�����Ϊdata �������������Ƶ������Ƶ�����������ټ���
							st->actx->codec_type = CODEC_TYPE_DATA;
							st->actx->codec_id = CODEC_ID_NONE;
							url_fskip(pb, size);
							break;
					}
				}
				break;
			default:  // skip tag
				//��������ʶ��Ŀ� chunk��������
				size += (size &1);
				url_fskip(pb, size);
				printf("url_fskip size:%d\n", size);
				break;
		}
	}

end_of_header:
	if (stream_index != s->nb_streams - 1) // check stream number
	{
fail:
		//У��������Ŀ����������ͷ������Դ������-1����
		for (i = 0; i < s->nb_streams; i++)
		{
			av_freep(&s->streams[i]->actx->extradata);
			av_freep(&s->streams[i]);
		}
		return  - 1;
	}

	//���� AVI�ļ�������
	printf("avi_load_index()\n");
	avi_load_index(s);

	//�б��Ƿ��Ƿǽ�֯ avi��
	avi->non_interleaved |= guess_ni_flag(s);
	if (avi->non_interleaved){
		//����Щ�ǽ�֯�洢��ý�������˹��Ĳ������������ڶ�ȡ������
		//printf("clean_index()\n");
		clean_index(s);
	}
	return 0;
}

//avi �ļ����Լ���Ϊ����Ƶý������ʱ�����ͬ���������Ƶ������Ҫͬ����ȡ��ͬ�����룬���Ų� ��ͬ����
//��֯�洢�� avi �ļ�:
//�ٽ��洢������Ƶ֡����ʱ���ʾʱ�������
//΢С�Ľ���ʱ���ʾʱ���������֡������е��������Կ��Լ򵥵İ����ļ�˳���ȡý�����ݡ�
//�ǽ�֯�洢�� avi �ļ�:
//��Ƶ����Ƶ������ý�����������Զ��С����򵥵�˳����ļ�ʱ��
//����ͬʱ������Ƶ����Ƶ���ݣ�����²�ͬ���� ffplay ��ȡ�����ʱ�������������Ƶ������Ƶ����
int avi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
	AVIContext *avi = (AVIContext*)s->priv_data;
	ByteIOContext *pb = &s->pb;
	int n, d[8], size;
	offset_t i, sync;

	if (avi->non_interleaved)
	{
		//����Ƿǽ�֯ AVI�������ʱ�����������ȡ��Ƶ������Ƶ���ݡ�
		int best_stream_index = 0;
		AVStream *best_st = NULL;
		AVIStream *best_ast;
		int64_t best_ts = INT64_MAX;
		int i;
		//��������ý�����������Ѿ����ŵ������ݣ�������һ�������ʱ��㡣
		for (i = 0; i < s->nb_streams; i++)
		{
			AVStream *st = s->streams[i];
			AVIStream *ast = (AVIStream*)st->priv_data;
			int64_t ts = ast->frame_offset;
			//��֡ƫ�ƻ����֡����
			if (ast->sample_size)
				ts /= ast->sample_size;

			//��֡�������pts��ʾʱ�䡣
			ts = av_rescale(ts, AV_TIME_BASE *(int64_t)st->time_base.num, st->time_base.den);
			//ȡ��С��ʱ����Ӧ��ʱ�䣬��ָ�룬��������ΪҪ��ȡ����� (��ȡ)��������
			//ÿ�ζ�ȡʱ���(ast->frame_offset)����İ�
			if (ts < best_ts)
			{
				best_ts = ts;
				best_st = st;
				best_stream_index = i;
			}
		}
		//�����������Ӧ�� AVIStream������432 �и�ֵ�����ݲ���packet_size ��remaining��
		best_ast = (AVIStream*)best_st->priv_data;
		//������С��ʱ��㣬����������ȡ����Ӧ��������
		//�ڻ����㹻��һ����������ȡ֡����ʱ����ʱ best_ast->remaining ����Ϊ0��
		best_ts = av_rescale(best_ts, best_st->time_base.den, AV_TIME_BASE *(int64_t)best_st->time_base.num);
		if (best_ast->remaining)
			i = av_index_search_timestamp(best_st, best_ts, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
		else
			i = av_index_search_timestamp(best_st, best_ts, AVSEEK_FLAG_ANY);

		if (i >= 0)
		{
			//�ҵ����������ȡ������������ �� 426�� seek����Ӧλ�ã��� 430�б����������������432 �б���
			//������Ҫ��ȡ�����ݴ�С(ͨ������������ҵ�����������ҵ���ӦAVIStream �ṹ�����ҵ����ݴ�С)��
			int64_t pos = best_st->index_entries[i].pos;
			pos += best_ast->packet_size - best_ast->remaining;
			url_fseek(&s->pb, pos + 8, SEEK_SET);//ԭ��426��
			assert(best_ast->remaining <= best_ast->packet_size);
			avi->stream_index_2 = best_stream_index;//ԭ��430��
			if (!best_ast->remaining)
				best_ast->packet_size = best_ast->remaining = best_st->index_entries[i].size;//ԭ��432��
		}
	}

resync:

	if (avi->stream_index_2 >= 0)
	{
		//����ҵ�������������Դ�Ϊ��������ȡ�����������Ͷ�ȡý�����ݡ�
		AVStream *st = s->streams[avi->stream_index_2];
		AVIStream *ast = (AVIStream*) st->priv_data;
		int size;

		if (ast->sample_size <= 1) // minorityreport.AVI block_align=1024 sample_size=1 IMA-ADPCM
			size = INT_MAX;
		else if (ast->sample_size < 32)
			size = 64 * ast->sample_size;
		else
			size = ast->sample_size;

		//�ڻ����㹻��һ��ȫ����ȡһ֡ý�����ݵ�����£� 451 ���жϲ������� size ���� ast->sample_size
		if (size > ast->remaining){//ԭ��451��
			size = ast->remaining;
		}

		//���� av_get_packet()����ʵ�ʶ�ȡý�����ݵ�pkt ���С�
		av_get_packet(pb, pkt, size);

		//�޸�ý������һЩ����������
		pkt->dts = ast->frame_offset;

		if (ast->sample_size)
			pkt->dts /= ast->sample_size;

		pkt->stream_index = avi->stream_index_2;
		//�ڼ����˳�򲥷�ʱ������ifû��ʲôʵ�����塣
		if (st->actx->codec_type == CODEC_TYPE_VIDEO)
		{
			if (st->index_entries)
			{
				AVIndexEntry *e;
				int index;
				index = av_index_search_timestamp(st, pkt->dts, 0);
				e = &st->index_entries[index];
				if (index >= 0 && e->timestamp == ast->frame_offset)
				{
					if (e->flags &AVINDEX_KEYFRAME)
						pkt->flags |= PKT_FLAG_KEY;
				}
			}
			else
			{
				pkt->flags |= PKT_FLAG_KEY; // if no index, better to say that all frames are key frames
			}
		}
		else
		{
			//���û���������Ϻõİ취�ǰ�����֡����Ϊ�ؼ�֡��
			pkt->flags |= PKT_FLAG_KEY;
		}

		//�޸�֡ƫ�ơ�
		if (ast->sample_size)
			ast->frame_offset += pkt->size;
		else
			ast->frame_offset++;

		ast->remaining -= size;
		if (!ast->remaining)
		{
			//�����㹻��ʱ������һ���ܵ������λ��־�Բ�����
			avi->stream_index_2 =  - 1;
			ast->packet_size = 0;
			if (size &1)
			{
				get_byte(pb);
				size++;
			}
		}
		//����ʵ�ʶ��������ݴ�С��
		return size;
	}

	//������ d[8]��Ϊ-1��Ϊ�������������ǲ���ʱ���������
	memset(d,  - 1, sizeof(int) *8);
	for (i = sync = url_ftell(pb); !url_feof(pb); i++)
	{
		//��֯ aviʱ˳���ȡ�ļ���ý�����ݡ�
		int j;

		if (i >= avi->movi_end)
			break;
		//����Ҫ�ҵ�����ǣ����� 00db,00dc,01wb�ȡ���32bit CPU ��Ϊ��ȡ���ݷ��㣬
		//��avi�ļ��е�֡��Ǻ�֡��С�� 8���ֽڶ�Ӧ��ֵ�� int������d[8]�У�����ÿ��������������
		for (j = 0; j < 7; j++)
			d[j] = d[j + 1];

		//��������ͻ���ǰ��һ����λ��
		//������ļ��ж�һ���ֽڲ��䵽���ͻ��棬�������С����������
		d[7] = get_byte(pb);

		size = d[4] + (d[5] << 8) + (d[6] << 16) + (d[7] << 24);

		if (d[2] >= '0' && d[2] <= '9' && d[3] >= '0' && d[3] <= '9')
		{
			n = (d[2] - '0') *10+(d[3] - '0');
		}
		else
		{
			n = 100; //invalid stream id
		}
		//У��size ��С�����ƫ��λ�ü�size �������ݿ��С�Ͳ�����Ч������ǡ�
		//У�������������<0�Ͳ�����Ч������ǡ���������0��ʼ������ý���ļ�ͨ��������10����
		if (i + size > avi->movi_end || d[0] < 0)
			continue;

		//��������junk����Ҫ�����Ŀ顣
		if ((d[0] == 'i' && d[1] == 'x' && n < s->nb_streams)
				|| (d[0] == 'J' && d[1] == 'U' && d[2] == 'N' && d[3] == 'K'))
		{
			url_fskip(pb, size);
			goto resync;
		}

		//������������n��
		if (d[0] >= '0' && d[0] <= '9' && d[1] >= '0' && d[1] <= '9')
		{
			n = (d[0] - '0') *10+(d[1] - '0');
		}
		else
		{
			n = 100; //invalid stream id
		}

		//parse ##dc/##wb
		if (n < s->nb_streams)
		{
			//�����������n��������С����Ϊ��Ч�� (�Ҹ�����Ϊ���У�鲻̫�ϸ� )
			AVStream *st;
			AVIStream *ast;
			st = s->streams[n];
			ast = (AVIStream*) st->priv_data;

			if(sync + 9 <= i)
			{
				int dbg=0;
			}
			else
			{
				int dbg1=0;
			}

			if (((ast->prefix_count < 5 || sync + 9 > i) && d[2] < 128 && d[3] < 128)
					|| d[2] * 256 + d[3] == ast->prefix)
			{
				//if(d[2]*256+d[3]==ast->prefix)Ϊ���ʾ "db","dc","wb"���ִ�ƥ�䣬�ҵ���ȷ֡��ǡ�
				//�ж� d[2]<128 && d[3]<128 ����Ϊ 'd','b','c','w'���ַ��� ascii��С�� 128��
				//�ж� ast->prefix_count<5 || sync + 9 > i�����жϵ�һý��� 5֡�ڻ���֡��ǳ��� 9���ֽڡ�
				//563 �е�569 ���ǵ�һý��֡�߽����ʶ��ɹ����Ժ�ʶ��ɹ��ļ򵥴��������������򱣴��ǡ�
				if (d[2] * 256 + d[3] == ast->prefix){//ԭ��563��
					ast->prefix_count++;
				}
				else
				{
					ast->prefix = d[2] *256+d[3];
					ast->prefix_count = 0;
				}//ԭ��569��

				//�ҵ���Ӧ���������󣬱�����ز�������ת��ʵ���Զ�ý�����
				avi->stream_index_2 = n;
				ast->packet_size = size + 8;
				ast->remaining = size;
				goto resync;
			}
		}

		// palette changed chunk
		if (d[0] >= '0' && d[0] <= '9' && d[1] >= '0' && d[1] <= '9'
				&& (d[2] == 'p' && d[3] == 'c') && n < s->nb_streams && i + size <= avi->movi_end)
		{
			//������ɫ��ı�����ݣ���ȡ��ɫ�����ݵ�������������ĵĵ�ɫ�������С�
			AVStream *st;
			int first, clr, flags, k, p;

			st = s->streams[n];

			first = get_byte(pb);
			clr = get_byte(pb);
			if (!clr) // all 256 colors used
				clr = 256;
			flags = get_le16(pb);
			p = 4;
			for (k = first; k < clr + first; k++)
			{
				int r, g, b;
				r = get_byte(pb);
				g = get_byte(pb);
				b = get_byte(pb);
				get_byte(pb);
				st->actx->palctrl->palette[k] = b + (g << 8) + (r << 16);
			}
			st->actx->palctrl->palette_changed = 1;
			goto resync;
		}
	}

	return  - 1;
}

//ʵ�ʶ�ȡAVI�ļ���������
static int avi_read_idx1(AVFormatContext *s, int size)
{
	AVIContext *avi = (AVIContext*) s->priv_data;
	ByteIOContext *pb = &s->pb;
	int nb_index_entries, i;
	AVStream *st;
	AVIStream *ast;
	unsigned int index, tag, flags, pos, len;
	unsigned last_pos =  - 1;

	//���û��������chunk��ֱ�ӷ��ء�
	nb_index_entries = size / 16;
	if (nb_index_entries <= 0)
		return  - 1;

	//�������������
	for (i = 0; i < nb_index_entries; i++)// read the entries and sort them in each stream component
	{
		tag = get_le32(pb);
		flags = get_le32(pb);
		pos = get_le32(pb);
		len = get_le32(pb);
		//�����һ������ָʾ��ƫ�����������ݿ��ƫ������������ָʾ��ƫ����������ļ���ʼ�ֽڵ�ƫ������
		//�������ص��ڴ��
		//�����������ݿ��ƫ������Ҫ�����������ļ���ʼ�ֽڵ�ƫ������ ����seek
		//�������� 631�к� 633��ͳһ���������������
		if (i == 0 && pos > avi->movi_list){//ԭ��631��
			avi->movi_list = 0;
		}

		pos += avi->movi_list;//ԭ��633��

		//������ID�����������е�00dc�� 01wb�ȹؼ��ֱ�ʾ����ID�ֱ�Ϊ����0��1��
		index = ((tag &0xff) - '0') *10;
		index += ((tag >> 8) &0xff) - '0';
		if (index >= s->nb_streams)
			continue;

		st = s->streams[index];
		ast = (AVIStream*) st->priv_data;

		if (last_pos == pos)
			avi->non_interleaved = 1;
		else
			av_add_index_entry(st, pos, ast->cum_len, len, 0, (flags &AVIIF_INDEX) ? AVINDEX_KEYFRAME : 0);

		if (ast->sample_size)
			ast->cum_len += len / ast->sample_size;
		else
			ast->cum_len++;
		last_pos = pos;
	}
	return 0;
}

//�ж��Ƿ��Ƿǽ�֯���ý�����ݣ�����ni ��non_interleaved ����д���ǽ�֯����˼��
//����Ƿǽ�֯��ŷ��� 1����֯��ŷ���0��
//�ǽ�֯��ŵ� avi �ļ�������ж��ý�������϶���ĳ�����Ŀ�ʼ�ֽ��ļ�ƫ������������ĳ������
//ĩβ�ֽڵ��ļ�ƫ��������������������ж��Ƿ��Ƿǽ�֯��ţ������϶�Ϊ��֯��š�
static int guess_ni_flag(AVFormatContext *s)
{
	int i;
	int64_t last_start = 0;
	int64_t first_end = INT64_MAX;
	//����AVI�ļ������е�������ȡ����ʼƫ���������ֵ��ĩβƫ��������Сֵ�жϡ�
	for (i = 0; i < s->nb_streams; i++)
	{
		AVStream *st = s->streams[i];
		int n = st->nb_index_entries;
		//���ĳ����û��index���Ϊ�����û�����ݣ���������Բ��ơ�
		if (n <= 0)
			continue;
		//����AVI�ļ������е�������ȡ����ʼƫ���������ֵ��
		if (st->index_entries[0].pos > last_start)
			last_start = st->index_entries[0].pos;
		//����AVI�ļ������е�������ȡ��ĩβƫ��������Сֵ��
		if (st->index_entries[n - 1].pos < first_end)
			first_end = st->index_entries[n - 1].pos;
	}
	//���ĳ�����Ŀ�ʼ���ֵ����ĳ������ĩβ��Сֵ����Ϊ�Ƿǽ�֯�洢�������ǽ�֯�洢��
	return last_start > first_end;
}

//����AVI�ļ�������chunk�� �ر�ע����avi_read_idx1()�������õ� av_add_index_entry()�����Ƿ�ý
//�����Ͱ���ʱ��˳�����������
static int avi_load_index(AVFormatContext *s)
{
	AVIContext *avi = (AVIContext*)s->priv_data;
	ByteIOContext *pb = &s->pb;
	uint32_t tag, size;
	offset_t pos = url_ftell(pb);

	url_fseek(pb, avi->movi_end, SEEK_SET);

	for (;;)
	{
		if (url_feof(pb))
			break;
		tag = get_le32(pb);
		size = get_le32(pb);

		switch (tag)
		{
			case MKTAG('i', 'd', 'x', '1'):
				if (avi_read_idx1(s, size) < 0)
				goto skip;
				else
					goto the_end;
				break;
			default:
skip:
				size += (size &1);
				url_fskip(pb, size);
				break;
		}
	}
the_end:
	url_fseek(pb, pos, SEEK_SET);
	return 0;
}

//�ر�AVI�ļ����ͷ��ڴ�����������Դ��
static int avi_read_close(AVFormatContext *s)
{
	int i;
	AVIContext *avi = (AVIContext*)s->priv_data;

	for (i = 0; i < s->nb_streams; i++)
	{
		AVStream *st = s->streams[i];
		AVIStream *ast = (AVIStream*)st->priv_data;
		av_free(ast);
		av_free(st->actx->extradata);
		av_free(st->actx->palctrl);
	}

	return 0;
}

//AVI �ļ��жϣ�ȡAVI �ļ��Ĺؼ��ִ�"RIFF"��"AVI"�жϣ���get_riff()����������ͬ��
static int avi_probe(AVProbeData *p)
{
	if (p->buf_size <= 32) // check file header
		return 0;
	if (p->buf[0] == 'R' && p->buf[1] == 'I' && p->buf[2] == 'F' && p->buf[3] == 'F'
			&& p->buf[8] == 'A' && p->buf[9] == 'V' && p->buf[10] == 'I'&& p->buf[11] == ' ')
		return AVPROBE_SCORE_MAX;
	else
		return 0;
}

//��ʼ��AVI�ļ���ʽ AVInputFormat�ṹ��ֱ�ӵĸ�ֵ������
AVInputFormat avi_iformat =
{
	"avi",
	sizeof(AVIContext),
	avi_probe,
	avi_read_header,
	avi_read_packet,
	avi_read_close,
};

//ע��avi�ļ���ʽ�� ffplay ������֧�ֵ��ļ���ʽ������������������ͷ��first_iformat�� ���ڲ��ҡ�
int avidec_init(void)
{
	av_register_input_format(&avi_iformat);
	return 0;
}

/*
   AVIF_HASINDEX��������AVI�ļ���"idx1"��
   AVIF_MUSTUSEINDEX���������������������ָ������˳��
   AVIF_ISINTERLEAVED��������AVI�ļ���interleaved��ʽ��
   AVIF_WASCAPTUREFILE��������AVI�ļ����ò�׽ʵʱ��Ƶר�ŷ�����ļ�
   AVIF_COPYRIGHTED��������AVI�ļ������а�Ȩ��Ϣ

AVIF_MUSTUSEINDEX : ����Ӧ�ó�����Ҫʹ��index�������������ϵ�˳�����������ݵ�չ��˳��
���磬�ñ�־�������ڴ���һ���༭�õ�֡�б���
// */