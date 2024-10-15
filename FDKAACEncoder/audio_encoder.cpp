#include "audio_encoder.h"

#define LOG_TAG "AudioEncoder"

AudioEncoder::AudioEncoder() {
}

AudioEncoder::~AudioEncoder() {
}

int AudioEncoder::alloc_audio_stream(const char * codec_name) {
	AVCodec *codec;
    // 设置采样格式
	AVSampleFormat preferedSampleFMT = AV_SAMPLE_FMT_S16;
    // 设置声道数
	int preferedChannels = audioChannels;
    // 设置采样率
	int preferedSampleRate = audioSampleRate;
    // 创建一个新的音频流
	audioStream = avformat_new_stream(avFormatContext, NULL);
    // 设置音频流的id
	audioStream->id = 1;
    // 获取音频流的编码器
	avCodecContext = audioStream->codec;
    // 设置编码器的媒体类型为音频
	avCodecContext->codec_type = AVMEDIA_TYPE_AUDIO;
    // 设置编码器的采样率
	avCodecContext->sample_rate = audioSampleRate;
    // 设置编码器的比特率
	if (publishBitRate > 0) {
		avCodecContext->bit_rate = publishBitRate;
	} else {
		avCodecContext->bit_rate = PUBLISH_BITE_RATE;
	}
    // 设置编码器的格式
	avCodecContext->sample_fmt = preferedSampleFMT;
	LOGI("audioChannels is %d", audioChannels);
    // 设置编码器的声道布局为单声道或立体声
	avCodecContext->channel_layout = preferedChannels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    // 获取声道数量并赋予编码器
	avCodecContext->channels = av_get_channel_layout_nb_channels(avCodecContext->channel_layout);
    // 设置配置文件为AAC低复杂度配置
	avCodecContext->profile = FF_PROFILE_AAC_LOW;
	LOGI("avCodecContext->channels is %d", avCodecContext->channels);
    // 标识编码器拥有一个全局头部标志
	avCodecContext->flags |= CODEC_FLAG_GLOBAL_HEADER;

    /* find the MP3 encoder */
//    codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    // 通过名称寻找编码器
	codec = avcodec_find_encoder_by_name(codec_name);
	if (!codec) {
		LOGI("Couldn't find a valid audio codec");
		return -1;
	}
    // 设置编码器上下文的id为该编码器的id
	avCodecContext->codec_id = codec->id;

    // 如果找到的编码器存在支持的采样格式列表
	if (codec->sample_fmts) {
		/* check if the prefered sample format for this codec is supported.
		 * this is because, depending on the version of libav, and with the whole ffmpeg/libav fork situation,
		 * you have various implementations around. float samples in particular are not always supported.
		 */
        // 获取编码器支持的采样格式列表
		const enum AVSampleFormat *p = codec->sample_fmts;
        // 遍历列表，与音频流的编码器的采样格式做比较
		for (; *p != -1; p++) {
			if (*p == audioStream->codec->sample_fmt)
				break;
		}
        // 没找到匹配的采样格式
		if (*p == -1) {
			LOGI("sample format incompatible with codec. Defaulting to a format known to work.........");
			/* sample format incompatible with codec. Defaulting to a format known to work */
            // 将编码器上下文的采样格式设置为编码器支持的第一个采样格式
			avCodecContext->sample_fmt = codec->sample_fmts[0];
		}
	}

    // 如果找到的编码器存在支持的采样率列表
	if (codec->supported_samplerates) {
        // 获取编码器支持的采样率列表
		const int *p = codec->supported_samplerates;
		int best = 0;
		int best_dist = INT_MAX;
        // 遍历，找到最接近音频流的编码器的采样率
		for (; *p; p++) {
			int dist = abs(audioStream->codec->sample_rate - *p);
			if (dist < best_dist) {
				best_dist = dist;
				best = *p;
			}
		}
		/* best is the closest supported sample rate (same as selected if best_dist == 0) */
        // 设置采样率
		avCodecContext->sample_rate = best;
	}
    // 如果首选的声道数或采样率或采样格式与编码器上下文的不一致
	if ( preferedChannels != avCodecContext->channels
			|| preferedSampleRate != avCodecContext->sample_rate
			|| preferedSampleFMT != avCodecContext->sample_fmt) {
		LOGI("channels is {%d, %d}", preferedChannels, audioStream->codec->channels);
		LOGI("sample_rate is {%d, %d}", preferedSampleRate, audioStream->codec->sample_rate);
		LOGI("sample_fmt is {%d, %d}", preferedSampleFMT, audioStream->codec->sample_fmt);
		LOGI("AV_SAMPLE_FMT_S16P is %d AV_SAMPLE_FMT_S16 is %d AV_SAMPLE_FMT_FLTP is %d", AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLTP);
        // 创建一个重采样上下文，通过输出声道布局、输出样本格式、输出采样率、输入声道布局、输入样本格式、输入采样率、日志级别偏移量、父日志上下文这些参数创建
		swrContext = swr_alloc_set_opts(NULL,
						av_get_default_channel_layout(avCodecContext->channels),
						(AVSampleFormat)avCodecContext->sample_fmt, avCodecContext->sample_rate,
						av_get_default_channel_layout(preferedChannels),
						preferedSampleFMT, preferedSampleRate,
						0, NULL);
        // 进行初始化，如果失败，则释放重采样上下文
		if (!swrContext || swr_init(swrContext)) {
			if (swrContext)
				swr_free(&swrContext);
			return -1;
		}
	}
    // 打开编码器，如果失败则返回
	if (avcodec_open2(avCodecContext, codec, NULL) < 0) {
		LOGI("Couldn't open codec");
		return -2;
	}
    // 设置编码器上下文的时基的分子
	avCodecContext->time_base.num = 1;
    // 设置编码器上下文的时基的分母
	avCodecContext->time_base.den = avCodecContext->sample_rate;
    // 设置编码器上下文的帧大小
	avCodecContext->frame_size = 1024;
	return 0;

}

int AudioEncoder::alloc_avframe() {
	int ret = 0;
    // 声明首选格式
	AVSampleFormat preferedSampleFMT = AV_SAMPLE_FMT_S16;
    // 声明首选声道数
	int preferedChannels = audioChannels;
	// 声明首选采样率
    int preferedSampleRate = audioSampleRate;
    // 分配一个AVFrame结构体作为输入音频帧
	input_frame = av_frame_alloc();
    // 如果分配失败则返回
	if (!input_frame) {
		LOGI("Could not allocate audio frame\n");
		return -1;
	}
    // 设置输入音频帧的样本数为编码器上下文的帧大小
	input_frame->nb_samples = avCodecContext->frame_size;
    // 设置输入音频帧的格式为首选采样格式
	input_frame->format = preferedSampleFMT;
    // 设置输入音频帧的声道布局为单声道或立体声
	input_frame->channel_layout = preferedChannels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    // 设置输入音频帧的采样率为首选采样率
	input_frame->sample_rate = preferedSampleRate;
    // 计算音频缓冲区的大小，通过存储结果的指针、音频的声道数量、每个声道中的样本数量、首选样本格式、缓冲区大小对齐要求
	buffer_size = av_samples_get_buffer_size(NULL, av_get_channel_layout_nb_channels(input_frame->channel_layout),
			input_frame->nb_samples, preferedSampleFMT, 0);
    // 分配对应的空间
	samples = (uint8_t*) av_malloc(buffer_size);
    // 设置指针初始的位置
	samplesCursor = 0;
	if (!samples) {
		LOGI("Could not allocate %d bytes for samples buffer\n", buffer_size);
		return -2;
	}
	LOGI("allocate %d bytes for samples buffer\n", buffer_size);
	/* setup the data pointers in the AVFrame */
    // 将分配的音频样本缓冲区与输入音频帧关联起来，通过输入音频帧、通过音频帧的声道布局获取到的声道数量、首选样本格式、缓冲区、缓冲区大小、对齐方式进行关联
	ret = avcodec_fill_audio_frame(input_frame, av_get_channel_layout_nb_channels(input_frame->channel_layout),
			preferedSampleFMT, samples, buffer_size, 0);
	if (ret < 0) {
		LOGI("Could not setup audio frame\n");
	}
    // 如果存在重采样上下文
	if(swrContext) {
        // 检查编解码器上下文的采样格式是否为平面格式
		if (av_sample_fmt_is_planar(avCodecContext->sample_fmt)) {
			LOGI("Codec Context SampleFormat is Planar...");
		}
		/* 分配空间 */
		convert_data = (uint8_t**)calloc(avCodecContext->channels,
			        sizeof(*convert_data));
        // 为音频样本分配一个缓冲区，并相应地填充数据指针和行大小。通过存放输出的指针数组、存储对齐后的音频缓冲区大小的指针、声道数量、样本大小、采样格式、对齐方式进行处理
		av_samples_alloc(convert_data, NULL,
				avCodecContext->channels, avCodecContext->frame_size,
				avCodecContext->sample_fmt, 0);
        // 计算重采样缓冲区的大小。通过行大小、声道数、帧大小、采样格式、对齐方式计算
		swrBufferSize = av_samples_get_buffer_size(NULL, avCodecContext->channels, avCodecContext->frame_size, avCodecContext->sample_fmt, 0);
        // 分配重采样的内存空间
		swrBuffer = (uint8_t *)av_malloc(swrBufferSize);
		LOGI("After av_malloc swrBuffer");
		/* 此时data[0],data[1]分别指向frame_buf数组起始、中间地址 */
        // 分配一个AVFrame结构体作为重采样后的音频帧
		swrFrame = av_frame_alloc();
		if (!swrFrame) {
			LOGI("Could not allocate swrFrame frame\n");
			return -1;
		}
        // 设置样本数量
		swrFrame->nb_samples = avCodecContext->frame_size;
        // 设置采样格式
		swrFrame->format = avCodecContext->sample_fmt;
        // 设置声道布局
		swrFrame->channel_layout = avCodecContext->channels == 1 ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
        // 设置采样率
		swrFrame->sample_rate = avCodecContext->sample_rate;
        // 将重采样缓冲区与音频帧关联起来。通过输入音频帧、通过音频帧的声道布局获取到的声道数量、首选样本格式、缓冲区、缓冲区大小、对齐方式进行关联
		ret = avcodec_fill_audio_frame(swrFrame, avCodecContext->channels, avCodecContext->sample_fmt, (const uint8_t*)swrBuffer, swrBufferSize, 0);
		LOGI("After avcodec_fill_audio_frame");
		if (ret < 0) {
			LOGI("avcodec_fill_audio_frame error ");
		    return -1;
		}
	}
	return ret;
}

int AudioEncoder::init(int bitRate, int channels, int sampleRate, int bitsPerSample, const char* aacFilePath, const char * codec_name) {
	avCodecContext = NULL;
	avFormatContext = NULL;
	input_frame = NULL;
	samples = NULL;
	samplesCursor = 0;
	swrContext = NULL;
	swrFrame = NULL;
	swrBuffer = NULL;
	convert_data = NULL;
	this->isWriteHeaderSuccess = false;

	totalEncodeTimeMills = 0;
	totalSWRTimeMills = 0;
    
    // 设置发布的比特率为传入的比特率
	this->publishBitRate = bitRate;
    // 设置声道数为传入的声道数
	this->audioChannels = channels;
    // 设置采样率为传入的采样率
	this->audioSampleRate = sampleRate;
	int ret;
    // 注册所有可用的编码器
	avcodec_register_all();
    // 注册所有组件
	av_register_all();

    // 分配一个描述音频结构的结构体对象
	avFormatContext = avformat_alloc_context();
	LOGI("aacFilePath is %s ", aacFilePath);
    // 根据路径初始化一个描述输出格式的结构体对象
	if ((ret = avformat_alloc_output_context2(&avFormatContext, NULL, NULL, aacFilePath)) != 0) {
		LOGI("avFormatContext   alloc   failed : %s", av_err2str(ret));
		return -1;
	}

	/**
	 * decoding: set by the user before avformat_open_input().
	 * encoding: set by the user before avformat_write_header() (mainly useful for AVFMT_NOFILE formats).
	 * The callback should also be passed to avio_open2() if it's used to open the file.
	 */
    // 打开一个只写的字节流，并返回一个描述IO上下文的指针赋予pb
	if (ret = avio_open2(&avFormatContext->pb, aacFilePath, AVIO_FLAG_WRITE, NULL, NULL)) {
		LOGI("Could not avio open fail %s", av_err2str(ret));
		return -1;
	}

    // 根据编码器名称初始化音频流
	this->alloc_audio_stream(codec_name);
//	this->alloc_audio_stream("libfaac");
//	this->alloc_audio_stream("libvo_aacenc");
    // 打印结果
	av_dump_format(avFormatContext, 0, aacFilePath, 1);
	// write header
    // 为输出文件写入头文件信息
	if (avformat_write_header(avFormatContext, NULL) != 0) {
		LOGI("Could not write header\n");
		return -1;
	}
    // 标记已写入头文件信息成功
	this->isWriteHeaderSuccess = true;
    // 为音频编码分配和初始化一些必要的资源
	this->alloc_avframe();
	return 1;
}

int AudioEncoder::init(int bitRate, int channels, int bitsPerSample, const char* aacFilePath, const char * codec_name) {
	return init(bitRate, channels, 44100, bitsPerSample, aacFilePath, codec_name);
}

void AudioEncoder::encode(byte* buffer, int size) {
    // 已处理数据的游标指针
	int bufferCursor = 0;
    // 要读取的数量
	int bufferSize = size;
    // 如果缓冲区大小剩余的空间不够读取全部数量就执行循环
	while (bufferSize >= (buffer_size - samplesCursor)) {
        // 计算本次要复制的字节数
		int cpySize = buffer_size - samplesCursor;
        // 复制数据到样本缓冲区中
		memcpy(samples + samplesCursor, buffer + bufferCursor, cpySize);
        // 移动游标
		bufferCursor += cpySize;
        // 计算剩余要处理的大小
		bufferSize -= cpySize;
        // 编码操作
		this->encodePacket();
        // 重置游标
		samplesCursor = 0;
	}
    // 如果还有没读取完的数量，此时大小一定是小于剩余空间的，一定足够
	if (bufferSize > 0) {
        // 复制数据到样本缓冲区
		memcpy(samples + samplesCursor, buffer + bufferCursor, bufferSize);
        // 处理游标
		samplesCursor += bufferSize;
	}
}

void AudioEncoder::encodePacket() {
//	LOGI("begin encode packet..................");
	int ret, got_output;
	AVPacket pkt;
	av_init_packet(&pkt);
	AVFrame* encode_frame;
    // 如果存在重采样上下文
	if(swrContext) {
        // 将音频帧的数据转换到指定指针中。通过重采样上下文、输出指针、帧大小、输入帧的数据、输入帧大小进行转换
		swr_convert(swrContext, convert_data, avCodecContext->frame_size,
				(const uint8_t**)input_frame->data, avCodecContext->frame_size);
        // 计算长度
		int length = avCodecContext->frame_size * av_get_bytes_per_sample(avCodecContext->sample_fmt);
        // 将重采样后的数据复制到重采样帧中
		for (int k = 0; k < 2; ++k) {
			for (int j = 0; j < length; ++j) {
				swrFrame->data[k][j] = convert_data[k][j];
		    }
		}
        // 将需要编码的帧赋予变量
		encode_frame = swrFrame;
	} else {
        // 将需要编码的帧赋予变量
		encode_frame = input_frame;
	}
    // 设置流索引
	pkt.stream_index = 0;
    // 设置持续时间
	pkt.duration = (int) AV_NOPTS_VALUE;
    // 设置时间戳
	pkt.pts = pkt.dts = 0;
    // 设置数据
	pkt.data = samples;
    // 设置包大小
	pkt.size = buffer_size;
    // 使用编码器上下文对音频数据包进行编码。通过编码器上下文、包指针、编码帧、是否获取到了输出进行编码
	ret = avcodec_encode_audio2(avCodecContext, &pkt, encode_frame, &got_output);
	if (ret < 0) {
		LOGI("Error encoding audio frame\n");
		return;
	}
    // 如果获取到了输出
	if (got_output) {
        // 如果编码上下文的帧的时间戳是有效的
		if (avCodecContext->coded_frame && avCodecContext->coded_frame->pts != AV_NOPTS_VALUE)
            // 将数据包的时间戳调整为编码上下文的帧的时间戳
			pkt.pts = av_rescale_q(avCodecContext->coded_frame->pts, avCodecContext->time_base, audioStream->time_base);
        // 设置数据包的标志为关键帧标志
		pkt.flags |= AV_PKT_FLAG_KEY;
        // 更新持续时间
		this->duration = pkt.pts * av_q2d(audioStream->time_base);
		//此函数负责交错地输出一个媒体包。如果调用者无法保证来自各个媒体流的包正确交错，则最好调用此函数输出媒体包，反之，可以调用av_write_frame以提高性能。
		int writeCode = av_interleaved_write_frame(avFormatContext, &pkt);
	}
    // 释放包
	av_free_packet(&pkt);
//	LOGI("leave encode packet...");
}

void AudioEncoder::destroy() {
	LOGI("start destroy!!!");
	//这里需要判断是否删除resampler(重采样音频格式/声道/采样率等)相关的资源
	if (NULL != swrBuffer) {
		free(swrBuffer);
		swrBuffer = NULL;
		swrBufferSize = 0;
	}
	if (NULL != swrContext) {
		swr_free(&swrContext);
		swrContext = NULL;
	}
	if(convert_data) {
		av_freep(&convert_data[0]);
		free(convert_data);
	}
	if (NULL != swrFrame) {
		av_frame_free(&swrFrame);
	}
	if (NULL != samples) {
		av_freep(&samples);
	}
	if (NULL != input_frame) {
		av_frame_free(&input_frame);
	}
	if(this->isWriteHeaderSuccess) {
		avFormatContext->duration = this->duration * AV_TIME_BASE;
	    LOGI("duration is %.3f", this->duration);
	    av_write_trailer(avFormatContext);
	}
	if (NULL != avCodecContext) {
		avcodec_close(avCodecContext);
		av_free(avCodecContext);
	}
	if (NULL != avCodecContext && NULL != avFormatContext->pb) {
		avio_close(avFormatContext->pb);
	}
	LOGI("end destroy!!! totalEncodeTimeMills is %d totalSWRTimeMills is %d", totalEncodeTimeMills, totalSWRTimeMills);
}
