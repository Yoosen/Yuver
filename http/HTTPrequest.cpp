#include "HTTPrequest.h"

const std::unordered_set<std::string> HTTPrequest::DEFAULT_HTML{
            "/index", "/welcome", "/video", "/picture" };

void HTTPrequest::init() {
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HTTPrequest::isKeepAlive() const {
    if (header_.count("Connection") == 1) {
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

// 主流程由函数 parse 完成
bool HTTPrequest::parse(Buffer& buff) {
    const char CRLF[] = "\r\n";
    if (buff.readableBytes() <= 0) {    // readableBytes 缓冲区可以写入的字节
        return false;
    }
    //std::cout<<"parse buff start:"<<std::endl;
    //buff.printContent();
    //std::cout<<"parse buff finish:"<<std::endl;
    while (buff.readableBytes() && state_ != FINISH) {
        // std::search() 查找 buff内当前读写指针之间第一个 CRLF 出现的位置
        const char* lineEnd = std::search(buff.curReadPtr(), buff.curWritePtrConst(), CRLF, CRLF + 2);
        std::string line(buff.curReadPtr(), lineEnd);
        switch (state_)
        {
            case REQUEST_LINE:
                //std::cout<<"REQUEST: "<<line<<std::endl;
                if (!parseRequestLine_(line)) {
                    return false;
                }
                parsePath_();
                break;
            case HEADERS:
                parseRequestHeader_(line);
                if (buff.readableBytes() <= 2) {
                    state_ = FINISH;
                }
                break;
            case BODY:
                parseDataBody_(line);
                break;
            default:
                break;
        }
        if (lineEnd == buff.curWritePtr()) { break; }
        buff.updateReadPtrUntilEnd(lineEnd + 2);
    }
    return true;
}

void HTTPrequest::parsePath_() {
    if (path_ == "/") {
        path_ = "/index.html";
    }
    else {
        for (auto& item : DEFAULT_HTML) {
            if (item == path_) {
                path_ += ".html";
                break;
            }
        }
    }
}

// 解析请求行
bool HTTPrequest::parseRequestLine_(const std::string& line) {
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");    // GET /mix/76.html?name=kelvin&password=123456 HTTP/1.1
    // 上一个会匹配到 GET /mix/76.html?name=kelvin&password=123456 HTTP/1.1
    std::smatch subMatch;
    // 以括号的形式划分组别
    if (regex_match(line, subMatch, patten)) {
        method_ = subMatch[1];  // 匹配的 GET
        path_ = subMatch[2];    // 匹配的 url 地址 /mix/76.html?name=kelvin&password=123456
        version_ = subMatch[3]; // 匹配的 Http 版本
        state_ = HEADERS;       // 解析完请求头，下一步要解析头部
        return true;
    }
    return false;
}

// 解析请求头
void HTTPrequest::parseRequestHeader_(const std::string& line) {
    std::regex patten("^([^:]*): ?(.*)$");
    std::smatch subMatch;       // e.g. Host: www.fishbay.cn
    if (regex_match(line, subMatch, patten)) {
        header_[subMatch[1]] = subMatch[2]; // head 是个 unordered_map
    }
    else {
        state_ = BODY;  // 解析完成请求头，下一步要解析数据体
    }
}

// 解析数据体
void HTTPrequest::parseDataBody_(const std::string& line) {
    body_ = line;
    parsePost_();
    state_ = FINISH;    // 解析完请求体，解析完成
}

int HTTPrequest::convertHex(char ch) {
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return ch;
}

// 处理数据体的时候，如果格式是post，那么还需要解析post报文
void HTTPrequest::parsePost_() {
    // POST / HTTP1.1
    // Host:www.wrox.com
    // User-Agent:Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022)
    // Content-Type:application/x-www-form-urlencoded
    // Content-Length:40
    // Connection: Keep-Alive
    // 空行
    // name=Professional%20Ajax&publisher=Wiley
    if (method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
        if (body_.size() == 0) { return; }  // 数据体为 空，直接返回

        std::string key, value;
        int num = 0;
        int n = body_.size();
        int i = 0, j = 0;

        for (; i < n; i++) {
            char ch = body_[i];
            switch (ch) {
                case '=':
                    key = body_.substr(j, i - j);   // key = name; name=Professional%20Ajax&publisher=Wiley
                    j = i + 1;
                    break;
                case '+':
                    body_[i] = ' ';
                    break;
                case '%':
                    num = convertHex(body_[i + 1]) * 16 + convertHex(body_[i + 2]);
                    body_[i + 2] = num % 10 + '0';
                    body_[i + 1] = num / 10 + '0';
                    i += 2;
                    break;
                case '&':
                    value = body_.substr(j, i - j); // value = Professional; name=Professional%20Ajax&publisher=Wiley
                    j = i + 1;
                    post_[key] = value;
                    break;
                default:
                    break;
            }
        }
        assert(j <= i);
        // 最后的键值对 &publisher=Wiley
        if (post_.count(key) == 0 && j < i) {
            value = body_.substr(j, i - j);
            post_[key] = value;
        }
    }
}

std::string HTTPrequest::path() const {
    return path_;
}

std::string& HTTPrequest::path() {
    return path_;
}
std::string HTTPrequest::method() const {
    return method_;
}

std::string HTTPrequest::version() const {
    return version_;
}

std::string HTTPrequest::getPost(const std::string& key) const {
    assert(key != "");
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}

std::string HTTPrequest::getPost(const char* key) const {
    assert(key != nullptr);
    if (post_.count(key) == 1) {
        return post_.find(key)->second;
    }
    return "";
}