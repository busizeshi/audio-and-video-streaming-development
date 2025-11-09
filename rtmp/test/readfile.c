#include <stdio.h>
#include <stdlib.h> // For exit() and EXIT_FAILURE

// 我们一次读取的“块”大小
// 尝试把这个值改小 (比如 10)，看看输出有什么不同
#define BUFFER_SIZE 64

int main() {
    FILE *file;
    // 声明一个缓冲区，大小为 BUFFER_SIZE 字节
    char buffer[BUFFER_SIZE];
    size_t bytes_read; // 用于存储 fread 实际读取的字节数

    const char *filename = "/home/jwd/srs/trunk/AUTHORS.txt";

    // 1. 打开“书”(test.txt)，模式为 "rb" (以二进制方式读取)
    //    "rb" 是一个好习惯，即使是文本文件，它也能避免平台差异
    //    “书签”被自动放在第 0 字节处。
    file = fopen(filename, "rb");
    if (file == NULL) {
        perror("Error opening file"); // perror 会打印更具体的错误，比如 "No such file or directory"
        return EXIT_FAILURE;
    }

    printf("--- Start reading '%s' ---\n\n", filename);

    // 2. 循环读取
    //    这是关键：只要 fread 能读到大于0个字节，循环就继续
    //    (size_t item_size, size_t num_items, FILE *stream)
    //    我们这里是 (1, BUFFER_SIZE, file)，意思是：
    //    "请读取 64 个 '大小为1字节' 的项目"
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {

        // 第一次循环:
        //   - fread 从“书签”(位置0)开始，读取 64 字节到 buffer。
        //   - bytes_read 被设为 64。
        //   - “书签”自动移动到位置 64。
        // 第二次循环:
        //   - fread 从“书签”(位置64)开始，读取 64 字节到 buffer。
        //   - bytes_read 被设为 64。
        //   - “书签”自动移动到位置 128。
        // ...
        // 最后一次循环 (假设文件剩下 20 字节):
        //   - fread 从“书签”开始，尝试读 64 字节，但只读到了 20 字节。
        //   - fread 返回 20，所以 bytes_read 被设为 20。
        //   - “书签”移动到文件末尾。
        // 下一次循环:
        //   - fread 尝试读取，发现已在末尾，返回 0。
        //   - bytes_read 被设为 0，while 循环条件 (0 > 0) 为 false，循环终止。

        // 3. 将读取到的内容打印出来
        //    我们使用 fwrite 将 buffer 中的内容写入到 stdout (标准输出，即你的控制台)
        //    我们只打印我们“实际读取”的字节数 (bytes_read)，这非常重要！
        //    (因为 buffer 可能没有被填满，且 buffer 不是一个C字符串，它末尾没有 '\0')
        fwrite(buffer, 1, bytes_read, stdout);
    }

    printf("\n\n--- End of reading ---\n");

    // 4. 关闭“书”
    //    这会释放文件句柄，是一个必须的好习惯
    fclose(file);

    return 0;
}