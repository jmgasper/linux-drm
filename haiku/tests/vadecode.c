/*
 * Copyright 2026, air/OS. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*	Decodes an H.264 or HEVC Annex B stream with libavcodec on VA-API (the
	GPU) and prints an MD5 of every picture, its planes packed as FFmpeg's
	framemd5 muxer hashes rawvideo nv12/p010le: compare with
	"ffmpeg -i stream -f framemd5 -pix_fmt nv12 (or p010le)" on any machine,
	which decodes in software. H.264 and HEVC decoding is exact.

	vadecode h264|hevc <stream> [render node] */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/md5.h>
#include <libavutil/pixdesc.h>


static enum AVPixelFormat
choose_vaapi(AVCodecContext* context, const enum AVPixelFormat* formats)
{
	for (; *formats != AV_PIX_FMT_NONE; formats++) {
		if (*formats == AV_PIX_FMT_VAAPI)
			return *formats;
	}
	return AV_PIX_FMT_NONE;
}


static int sFrames;


static int
output(AVCodecContext* context, AVFrame* frame, AVFrame* software)
{
	for (;;) {
		int r = avcodec_receive_frame(context, frame);
		if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return 0;
		if (r < 0) {
			fprintf(stderr, "decoding failed: %s\n", av_err2str(r));
			return r;
		}
		if (frame->format != AV_PIX_FMT_VAAPI) {
			fprintf(stderr, "not decoded by the GPU\n");
			return -1;
		}
		av_frame_unref(software);
		r = av_hwframe_transfer_data(software, frame, 0);
		if (r < 0) {
			fprintf(stderr, "transfer: %s\n", av_err2str(r));
			return r;
		}
		int sample = software->format == AV_PIX_FMT_P010 ? 2 : 1;
		struct AVMD5* md5 = av_md5_alloc();
		uint8_t digest[16];
		av_md5_init(md5);
		for (int y = 0; y < frame->height; y++) {
			av_md5_update(md5, software->data[0] + y * software->linesize[0],
				frame->width * sample);
		}
		for (int y = 0; y < frame->height / 2; y++) {
			av_md5_update(md5, software->data[1] + y * software->linesize[1],
				frame->width * sample);
		}
		av_md5_final(md5, digest);
		av_free(md5);
		printf("%d %s ", sFrames++, av_get_pix_fmt_name(software->format));
		for (int i = 0; i < 16; i++)
			printf("%02x", digest[i]);
		printf("\n");
		av_frame_unref(frame);
	}
}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: vadecode h264|hevc <stream> [render node]\n");
		return 2;
	}
	int hevc = strcmp(argv[1], "hevc") == 0;
	const char* node = argc > 3 ? argv[3] : "/dev/dri/renderD128";
	FILE* file = fopen(argv[2], "rb");
	if (file == NULL) {
		perror(argv[2]);
		return 1;
	}

	AVBufferRef* device = NULL;
	int r = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, node, NULL,
		0);
	if (r < 0) {
		fprintf(stderr, "VA-API device %s: %s\n", node, av_err2str(r));
		return 1;
	}
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

	uint8_t buffer[65536 + AV_INPUT_BUFFER_PADDING_SIZE];
	memset(buffer + 65536, 0, AV_INPUT_BUFFER_PADDING_SIZE);
	int end = 0;
	while (!end) {
		size_t length = fread(buffer, 1, 65536, file);
		end = length == 0;
		uint8_t* data = buffer;
		do {
			r = av_parser_parse2(parser, context, &packet->data, &packet->size,
				data, (int)length, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
			if (r < 0)
				return 1;
			data += r;
			length -= r;
			if (packet->size > 0) {
				r = avcodec_send_packet(context, packet);
				if (r < 0 && r != AVERROR(EAGAIN)) {
					fprintf(stderr, "send: %s\n", av_err2str(r));
					return 1;
				}
				if (output(context, frame, software) < 0)
					return 1;
			}
		} while (length > 0);
	}
	avcodec_send_packet(context, NULL);
	if (output(context, frame, software) < 0)
		return 1;
	fprintf(stderr, "%d picture(s)\n", sFrames);
	return 0;
}
