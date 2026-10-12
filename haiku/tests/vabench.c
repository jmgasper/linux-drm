/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Where the time goes when decoded pictures are read back: decodes an
	H.264 or HEVC Annex B stream with libavcodec on VA-API and times, per
	picture, vaSyncSurface, vaGetImage into an image of the surface's own
	format, vaMapBuffer and reading the mapped image, and the same through
	av_hwframe_transfer_data (what the amduvd add-on does).

	vabench h264|hevc <stream> [pictures] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>


static double
now_ms()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}


static enum AVPixelFormat
choose_vaapi(AVCodecContext* context, const enum AVPixelFormat* formats)
{
	for (; *formats != AV_PIX_FMT_NONE; formats++) {
		if (*formats == AV_PIX_FMT_VAAPI)
			return *formats;
	}
	return AV_PIX_FMT_NONE;
}


static int sPictures, sLimit = 120;
static double sSync, sGet, sMap, sRead, sTransfer;
static unsigned sChecksum;


static void
measure(AVCodecContext* context, AVFrame* frame, AVFrame* software)
{
	AVHWDeviceContext* device = (AVHWDeviceContext*)context->hw_device_ctx->data;
	VADisplay display = ((AVVAAPIDeviceContext*)device->hwctx)->display;
	VASurfaceID surface = (VASurfaceID)(uintptr_t)frame->data[3];

	double t0 = now_ms();
	vaSyncSurface(display, surface);
	double t1 = now_ms();

	VAImage derived;
	VAImageFormat format;
	if (vaDeriveImage(display, surface, &derived) == VA_STATUS_SUCCESS) {
		format = derived.format;
		vaDestroyImage(display, derived.image_id);
	} else {
		memset(&format, 0, sizeof(format));
		format.fourcc = frame->hw_frames_ctx != NULL
			&& ((AVHWFramesContext*)frame->hw_frames_ctx->data)->sw_format
				== AV_PIX_FMT_P010 ? VA_FOURCC_P010 : VA_FOURCC_NV12;
	}
	VAImage image;
	if (vaCreateImage(display, &format, frame->width, frame->height, &image)
			!= VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaCreateImage failed\n");
		exit(1);
	}
	double t2 = now_ms();
	if (vaGetImage(display, surface, 0, 0, frame->width, frame->height,
			image.image_id) != VA_STATUS_SUCCESS) {
		fprintf(stderr, "vaGetImage failed\n");
		exit(1);
	}
	double t3 = now_ms();
	void* data;
	vaMapBuffer(display, image.buf, &data);
	double t4 = now_ms();
	unsigned sum = 0;
	for (unsigned i = 0; i < image.data_size; i += 64)
		sum += ((unsigned char*)data)[i];
	sChecksum += sum;
	double t5 = now_ms();
	vaUnmapBuffer(display, image.buf);
	vaDestroyImage(display, image.image_id);

	av_frame_unref(software);
	double t6 = now_ms();
	av_hwframe_transfer_data(software, frame, 0);
	double t7 = now_ms();

	sSync += t1 - t0;
	sGet += t3 - t2;
	sMap += t4 - t3;
	sRead += t5 - t4;
	sTransfer += t7 - t6;
	sPictures++;
	if (sPictures == 1) {
		printf("%dx%d, image %.1f MB\n", frame->width, frame->height,
			image.data_size / 1048576.0);
	}
}


static int
output(AVCodecContext* context, AVFrame* frame, AVFrame* software)
{
	for (;;) {
		int r = avcodec_receive_frame(context, frame);
		if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return 0;
		if (r < 0 || frame->format != AV_PIX_FMT_VAAPI)
			return -1;
		if (sPictures < sLimit)
			measure(context, frame, software);
		av_frame_unref(frame);
	}
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: vabench h264|hevc <stream> [pictures]\n");
		return 2;
	}
	if (argc > 3)
		sLimit = atoi(argv[3]);
	int hevc = strcmp(argv[1], "hevc") == 0;
	FILE* file = fopen(argv[2], "rb");
	if (file == NULL) {
		perror(argv[2]);
		return 1;
	}
	AVBufferRef* device = NULL;
	if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI,
			"/dev/dri/renderD128", NULL, 0) < 0)
		return 1;
	const AVCodec* codec = avcodec_find_decoder(hevc ? AV_CODEC_ID_HEVC
		: AV_CODEC_ID_H264);
	AVCodecContext* context = avcodec_alloc_context3(codec);
	context->hw_device_ctx = av_buffer_ref(device);
	context->get_format = choose_vaapi;
	context->thread_count = 1;
	if (avcodec_open2(context, codec, NULL) < 0)
		return 1;
	AVCodecParserContext* parser = av_parser_init(codec->id);
	AVPacket* packet = av_packet_alloc();
	AVFrame* frame = av_frame_alloc();
	AVFrame* software = av_frame_alloc();

	static uint8_t buffer[65536 + AV_INPUT_BUFFER_PADDING_SIZE];
	int end = 0;
	while (!end && sPictures < sLimit) {
		size_t length = fread(buffer, 1, 65536, file);
		end = length == 0;
		uint8_t* data = buffer;
		do {
			int r = av_parser_parse2(parser, context, &packet->data,
				&packet->size, data, (int)length, AV_NOPTS_VALUE,
				AV_NOPTS_VALUE, 0);
			if (r < 0)
				return 1;
			data += r;
			length -= r;
			if (packet->size > 0) {
				avcodec_send_packet(context, packet);
				if (output(context, frame, software) < 0)
					return 1;
			}
		} while (length > 0);
	}
	if (sPictures == 0)
		return 1;
	printf("%d pictures, per picture: sync %.2f ms, vaGetImage %.2f ms, "
		"map %.2f ms, read %.2f ms; av_hwframe_transfer_data %.2f ms "
		"(checksum %u)\n", sPictures, sSync / sPictures, sGet / sPictures,
		sMap / sPictures, sRead / sPictures, sTransfer / sPictures,
		sChecksum);
	return 0;
}
