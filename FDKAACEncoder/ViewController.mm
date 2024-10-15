//
//  ViewController.m
//  FDKAACEncoder
//
//  Created by apple on 2017/2/16.
//  Copyright © 2017年 xiaokai.zhan. All rights reserved.
//

#import "ViewController.h"
#import "CommonUtil.h"
#import "audio_encoder.h"
#include <stdio.h>

@interface ViewController ()

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view, typically from a nib.
}
- (IBAction)encode:(id)sender {
    NSLog(@"FDK AAC Encoder Test...");
    // 获取pcm文件路径
    NSString* pcmFilePath = [CommonUtil bundlePath:@"vocal.pcm"];
    // 获取aac文件路径
    NSString* aacFilePath = [CommonUtil documentsPath:@"vocal.aac"];
    // 创建编码器对象
    AudioEncoder* encoder = new AudioEncoder();
    // 设置每个样本的比特率为16
    int bitsPerSample = 16;
    // 设置编码器名称为fdkaac
    const char * codec_name = [@"libfdk_aac" cStringUsingEncoding:NSUTF8StringEncoding];
    // 设置比特率为128kbps
    int bitRate = 128* 1024;
    // 声道数为2
    int channels = 2;
    // 设置采样率为44100
    int sampleRate = 44100;
    // 根据参数初始化编码器
    encoder->init(bitRate, channels, sampleRate, bitsPerSample, [aacFilePath cStringUsingEncoding:NSUTF8StringEncoding], codec_name);
    // 设置缓冲空间为256kb
    int bufferSize = 1024 * 256;
    // 开辟缓冲空间
    byte* buffer = new byte[bufferSize];
    // 打开pcm文件句柄，设置为只读
    FILE* pcmFileHandle = fopen([pcmFilePath cStringUsingEncoding:NSUTF8StringEncoding], "rb");
    // 声明已读取的缓冲大小
    size_t readBufferSize = 0;
    // 每次读取256kb的数据，只要还能读取到数据循环就会持续
    while((readBufferSize = fread(buffer, 1, bufferSize, pcmFileHandle)) > 0) {
        // 对读取到的数据进行编码
        encoder->encode(buffer, (int)readBufferSize);
    }
    // 删除缓冲空间
    delete[] buffer;
    // 关闭文件句柄
    fclose(pcmFileHandle);
    // 销毁编码器
    encoder->destroy();
    // 删除编码器对象
    delete encoder;
}


- (void)didReceiveMemoryWarning {
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}


@end
