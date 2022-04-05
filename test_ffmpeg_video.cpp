#include <SDL2/SDL.h>
#include <SDL2/SDL_render.h>
#include <mutex>
#include <thread>
#include <cassert>
#include <atomic>

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavutil/avutil.h>
	#include <libswscale/swscale.h>
}

struct AppWindow {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	std::mutex mutex;
	AVFormatContext *format_ctx;
	AVCodecContext *codec_ctx;
	int video_index;
	std::thread thread;
	SwsContext *rescaler;
	std::atomic_bool stop;
};

AppWindow* app_window;

void start_decoding() {
	AVPacket *packet = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	AVFrame *out_frame = av_frame_alloc();

	while ((av_read_frame(app_window->format_ctx, packet)) >= 0 && !app_window->stop) {
		if (packet->stream_index != app_window->video_index) {
			av_packet_unref(packet);
			continue;
		}

		bool okay = false;

		while (!okay) {
			int ret = avcodec_send_packet(app_window->codec_ctx, packet);

			if (ret >= 0) {
				okay = true;
				av_packet_unref(packet);
			}

			while((ret = avcodec_receive_frame(app_window->codec_ctx, frame)) >= 0) {
				out_frame->width = frame->width;
				out_frame->height = frame->height;
				out_frame->format = AV_PIX_FMT_RGB565;

				av_frame_get_buffer(out_frame, 0);

				int result = sws_scale(app_window->rescaler, frame->data, frame->linesize, 0, frame->height, out_frame->data, out_frame->linesize);

				av_frame_unref(frame);

				if (result < 0) {
					assert(false);
				}

				std::unique_lock<std::mutex> _lock{app_window->mutex};
				void *pixels = nullptr;
				int pitch = 0;
				result = SDL_LockTexture(app_window->texture, NULL, &pixels, &pitch);
				if (result < 0) {
					assert(false);
				}
				// Copy the data
				// printf("FF Pitch: %d, SDL Pitch: %d\n", out_frame->linesize[0], pitch);
				memcpy(pixels, out_frame->data[0], out_frame->linesize[0] * out_frame->height);
				SDL_UnlockTexture(app_window->texture);
				av_frame_unref(out_frame);

				SDL_Delay(30);
			}
		}
	}

	av_frame_free(&frame);
	av_frame_free(&out_frame);
	av_packet_free(&packet);
}

void init_window() {
	app_window = new AppWindow();
	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		return;
	}

	SDL_Window *window = SDL_CreateWindow("FFmpeg Video Decoding", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_SHOWN);

	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	app_window->window = window;
	app_window->renderer = renderer;
	app_window->format_ctx = nullptr;

	if (avformat_open_input(&app_window->format_ctx, "/home/adebayo/Videos/8. Flutter ListView Widget.mkv", nullptr, nullptr) < 0) {
		abort();
	}

	if (avformat_find_stream_info(app_window->format_ctx, nullptr) < 0) {
		abort();
	}

	av_dump_format(app_window->format_ctx, -1, "/home/adebayo/Videos/8. Flutter ListView Widget.mkv", 0);

	AVCodec *codec{nullptr};

	int video_index = av_find_best_stream(app_window->format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
	if (video_index < 0) {
		abort();
	}

	app_window->video_index = video_index;

	app_window->codec_ctx = avcodec_alloc_context3(codec);
	if (!app_window->codec_ctx) {
		abort();
	}

	avcodec_parameters_to_context(app_window->codec_ctx, app_window->format_ctx->streams[video_index]->codecpar);

	int ret = avcodec_open2(app_window->codec_ctx, codec, nullptr);
	if (ret < 0) {
		abort();
	}

	SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, app_window->codec_ctx->width, app_window->codec_ctx->height);

	app_window->texture = texture;

	app_window->rescaler = sws_getContext(app_window->codec_ctx->width, app_window->codec_ctx->height, app_window->codec_ctx->pix_fmt, app_window->codec_ctx->width, app_window->codec_ctx->height, AV_PIX_FMT_RGB565, SWS_BICUBIC, nullptr, nullptr, nullptr);

	app_window->thread = std::thread{[]() { start_decoding(); }};
	app_window->stop = false;
	// app_window->thread.detach();
}

void draw() {
	SDL_SetRenderDrawColor(app_window->renderer, 255, 255, 255, 255);
	// Draw the texture here
	SDL_RenderClear(app_window->renderer);
	app_window->mutex.lock();
	SDL_RenderCopy(app_window->renderer, app_window->texture, nullptr, nullptr);
	app_window->mutex.unlock();
	SDL_RenderPresent(app_window->renderer);
}

void cleanup() {
	app_window->stop = true;

	app_window->thread.join();

	SDL_DestroyTexture(app_window->texture);
	SDL_DestroyRenderer(app_window->renderer);
	SDL_DestroyWindow(app_window->window);

	avformat_close_input(&app_window->format_ctx);
	avcodec_free_context(&app_window->codec_ctx);
	sws_freeContext(app_window->rescaler);

	SDL_Quit();
	delete app_window;
}

int main(int argc, char const *argv[]) {
	init_window();

	SDL_Event event;
	while (1) {
		if (SDL_PollEvent(&event)) {
			if (event.key.state == SDL_PRESSED && event.key.keysym.sym == SDLK_q || event.type == SDL_QUIT) {
				break;
			}
		}

		draw();

		SDL_Delay(30);
	}

	cleanup();

	return 0;
}