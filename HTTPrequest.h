#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>

#include "buffer.h"

class HTTPrequest
{
public:
    // 实现自动机的state变量
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,
    };

    enum HTTP_CODE {
        NO_REQUEST = 0,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURSE,
        FORBIDDENT_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION,
    };

    HTTPrequest() { init(); };
    ~HTTPrequest() = default;

    void init();
    // 主流程由函数parse完成
    bool parse(Buffer& buff); //解析HTTP请求

    //获取HTTP信息
    std::string path() const;   // 获取路径
    std::string& path();
    std::string method() const; // 获取HTTP方式
    std::string version() const;    // 获取版本
    std::string getPost(const std::string& key) const;
    std::string getPost(const char* key) const;

    bool isKeepAlive() const;   // HTTP连接是否KeepAlive的函数

private:
    bool parseRequestLine_(const std::string& line);//解析请求行
    void parseRequestHeader_(const std::string& line); //解析请求头
    void parseDataBody_(const std::string& line); //解析数据体


    void parsePath_();  // 解析请求行的时候，会解析出路径信息，之后还需要对路径信息做一个处理
    void parsePost_();  // 在处理数据体的时候，如果格式是post，那么还需要解析post报文，用函数parsePost实现

    static int convertHex(char ch);

    PARSE_STATE state_;     // 实现自动机的state变量
    std::string method_, path_, version_, body_;    // 存储HTTP方式、路径、版本和数据体
    std::unordered_map<std::string, std::string>header_;
    std::unordered_map<std::string, std::string>post_;

    static const std::unordered_set<std::string>DEFAULT_HTML;
};

#endif  //HTTP_REQUEST_H