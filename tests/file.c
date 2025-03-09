#include <stdio.h>
#include <stdlib.h>

char static *read_file_to_string(const char *filename)
{
  FILE *file = fopen(filename, "r");
  if (file == NULL)
  {
    fprintf(stderr, "无法打开 %s 文件\n", filename);
    return NULL;
  }

  // 获取文件大小
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  rewind(file);

  // 分配内存
  char *buffer = (char *)malloc(file_size + 1);
  if (buffer == NULL)
  {
    fclose(file);
    return NULL;
  }

  // 读取文件内容
  size_t read_size = fread(buffer, 1, file_size, file);
  buffer[read_size] = '\0'; // 添加字符串结束符

  fclose(file);
  return buffer;
}