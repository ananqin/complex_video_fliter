#include <stdio.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>

AVFilterContext *mainsrc_ctx = NULL;
AVFilterContext *resultsink_ctx = NULL;
AVFilterGraph *filter_graph = NULL;

int init_filters(const int width, const int height, const int format)
{
    int ret = 0;
    AVFilterInOut *inputs = NULL;
    AVFilterInOut *outputs = NULL;
    char filter_args[1024] = { 0 };

    filter_graph = avfilter_graph_alloc();
    if (!filter_graph) {
        printf("Error: allocate filter graph failed\n");
        return -1;
    }

    snprintf(filter_args, sizeof(filter_args),
             "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d[v0];" // Parsed_buffer_0
             "[v0]split[main][tmp];"        // Parsed_split_1
             "[tmp]crop=iw:ih/2:0:0,vflip[flip];"   // Parsed_crop_2 Parsed_vflip_3
             "[main][flip]overlay=0:H/2[result];" // Parsed_overlay_4
             "[result]buffersink", // Parsed_buffersink_5
             width, height, format, 1, 25, 1, 1);

//    snprintf(filter_args, sizeof(filter_args),
//             "buffer=video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d[v0];" // Parsed_buffer_0
//             "[v0]split[main][tmp];"        // Parsed_split_1
//             "[tmp]crop=iw:ih/2:0:0,vflip[flip];"  // Parsed_crop_2 Parsed_vflip_3
//             "[main]buffersink;" // Parsed_buffersink_4
//             "[flip]buffersink", // Parsed_buffersink_5
//             width, height, format, 1, 25, 1, 1);

    ret = avfilter_graph_parse2(filter_graph, filter_args, &inputs, &outputs);
    if (ret < 0) {
        printf("Cannot parse graph\n");
        return ret;
    }

    ret = avfilter_graph_config(filter_graph, NULL);    // 提交过滤器
    if (ret < 0) {
        printf("Cannot configure graph\n");
        return ret;
    }

    // Get AVFilterContext from AVFilterGraph parsing from string
    mainsrc_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_0");
    if(!mainsrc_ctx) {
        printf("avfilter_graph_get_filter Parsed_buffer_0 failed\n");
        return -1;
    }
    resultsink_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffersink_5");
    if(!resultsink_ctx) {
        printf("avfilter_graph_get_filter Parsed_buffersink_5 failed\n");
        return -1;
    }
    printf("sink_width:%d, sink_height:%d\n", av_buffersink_get_w(resultsink_ctx),
           av_buffersink_get_h(resultsink_ctx));


    return 0;
}
// ffmpeg -i 9.5.flv -vf "split[main][tmp];[tmp]crop=iw:ih/2:0:0,vflip [flip];[main][flip]overlay=0:H/2" -b:v 500k -vcodec libx264 9.5_out.flv
int main(int argc, char* argv)
{
    int ret = 0;
    int in_width = 768;
    int in_height = 320;

    avfilter_register_all();
    if(init_filters(in_width, in_height, AV_PIX_FMT_YUV420P) < 0) {
        printf("init_filters failed\n");
        return -1;
    }
    // input yuv
    FILE* inFile = NULL;
    const char* inFileName = "768x320.yuv";
    fopen_s(&inFile, inFileName, "rb+");
    if (!inFile) {
        printf("Fail to open file\n");
        return -1;
    }

    // output yuv
    FILE* outFile = NULL;
    const char* outFileName = "out_crop_vfilter_2.yuv";
    fopen_s(&outFile, outFileName, "wb");
    if (!outFile) {
        printf("Fail to create file for output\n");
        return -1;
    }

    char *graph_str = avfilter_graph_dump(filter_graph, NULL);
    FILE* graphFile = NULL;
    fopen_s(&graphFile, "graphFile.txt", "w");  // 打印filtergraph的具体情况
    fprintf(graphFile, "%s", graph_str);
    av_free(graph_str);

    AVFrame *frame_in = av_frame_alloc();
    unsigned char *frame_buffer_in = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
    av_image_fill_arrays(frame_in->data, frame_in->linesize, frame_buffer_in,
        AV_PIX_FMT_YUV420P, in_width, in_height, 1);

    AVFrame *frame_out = av_frame_alloc();
    unsigned char *frame_buffer_out = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, in_width, in_height, 1));
    av_image_fill_arrays(frame_out->data, frame_out->linesize, frame_buffer_out,
        AV_PIX_FMT_YUV420P, in_width, in_height, 1);

    frame_in->width = in_width;
    frame_in->height = in_height;
    frame_in->format = AV_PIX_FMT_YUV420P;
    uint32_t frame_count = 0;
    while (1) {
        // 读取yuv数据
        if (fread(frame_buffer_in, 1, in_width*in_height * 3 / 2, inFile) != in_width*in_height * 3 / 2) {
            break;
        }
        //input Y,U,V
        frame_in->data[0] = frame_buffer_in;
        frame_in->data[1] = frame_buffer_in + in_width*in_height;
        frame_in->data[2] = frame_buffer_in + in_width*in_height * 5 / 4;

        if (av_buffersrc_add_frame(mainsrc_ctx, frame_in) < 0) {
            printf("Error while add frame.\n");
            break;
        }
        // filter内部自己处理
        /* pull filtered pictures from the filtergraph */
        ret = av_buffersink_get_frame(resultsink_ctx, frame_out);
        if (ret < 0)
            break;

        //output Y,U,V
        if (frame_out->format == AV_PIX_FMT_YUV420P) {
            for (int i = 0; i < frame_out->height; i++) {
                fwrite(frame_out->data[0] + frame_out->linesize[0] * i, 1, frame_out->width, outFile);
            }
            for (int i = 0; i < frame_out->height / 2; i++) {
                fwrite(frame_out->data[1] + frame_out->linesize[1] * i, 1, frame_out->width / 2, outFile);
            }
            for (int i = 0; i < frame_out->height / 2; i++) {
                fwrite(frame_out->data[2] + frame_out->linesize[2] * i, 1, frame_out->width / 2, outFile);
            }
        }
        ++frame_count;
        if(frame_count % 25 == 0)
            printf("Process %d frame!\n",frame_count);
        av_frame_unref(frame_out);
    }

    fclose(inFile);
    fclose(outFile);

    av_frame_free(&frame_in);
    av_frame_free(&frame_out);
    avfilter_graph_free(&filter_graph); // 内部去释放AVFilterContext产生的内存
    printf("finish\n");

    return 0;
}
