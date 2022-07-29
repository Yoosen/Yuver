#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>  //open
#include <unistd.h> //close
#include <sys/stat.h> //stat
#include <sys/mman.h> //mmap,munmap
#include <assert.h>

#include "../buffer/buffer.h"

class HTTPresponse
{
public:
    HTTPresponse();
    ~HTTPresponse();

    void init(const std::string& srcDir, std::string& path, bool isKeepAlive = false, int code = -1);
    void makeResponse(Buffer& buffer);      // 生成响应报文的主函数
    void unmapFile_();
    char* file();
    size_t fileLen() const;
    void errorContent(Buffer& buffer, std::string message);     // 添加数据体的函数中，如果所请求的文件打不开，我们需要返回相应的错误信息，这个功能由函数
    int code() const { return code_; }      // 返回状态码的函数：


private:
    void addStateLine_(Buffer& buffer);         // 生成请求行
    void addResponseHeader_(Buffer& buffer);    // 生成请求头
    void addResponseContent_(Buffer& buffer);   // 生成数据体

    void errorHTML_();
    std::string getFileType_();                 // 在添加请求头的时候，我们需要得到文件类型信息

    int code_;      // 来代表 HTTP 的状态
    bool isKeepAlive_;  // HTTP连接是否处于KeepAlive状态

    std::string path_;      // path_代表解析得到的路径
    std::string srcDir_;    // srcDir_表示根目录，除此之外，我们还需要一个哈希表提供4XX状态码到响应文件路径的映射

    char* mmFile_;
    struct stat mmFileStat_;

    static const std::unordered_map<std::string, std::string> SUFFIX_TYPE;  // 哈希表SUFFIX_TYPE表示后缀名到文件类型的映射关系
    static const std::unordered_map<int, std::string> CODE_STATUS;      // 状态码到相应状态(字符串类型)的映射
    static const std::unordered_map<int, std::string> CODE_PATH;

};

#endif //HTTP_RESPONSE_H