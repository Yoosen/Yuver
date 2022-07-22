#include "http_conn.h"
#include "../log/log.h"
#include <map>
#include <mysql/mysql.h>
#include <fstream>


#define connfdLT    // 水平触发阻塞
#define listenfdLT  // 水平触发阻塞

// 定义http相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

const char *doc_root = "/home/yoosen/Yuver/docs";

// 将表中的用户名和密码放入 map
map<string, string> users;
locker m_lock;

void http_conn::initmysql_result(connection_pool *connPoll) {
   
    // 先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    // 执行查询语句
    if(mysql_query(mysql, "SELECT username,passwd FROM user")) {
        // mysql_error拿到上一次出错的信息
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    // 从表中检索完整的结果集，拿到查询结果
    MYSQL_RES *result = mysql_store_result(mysql);

    // 返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_row(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while(MYSQL_ROW row = mysql_fetch_row(result)) {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
/* 将文件描述符设置为非阻塞
 * 每个使用Epoll ET模式的文件描述符都应该是非阻塞的，
 * 否则读写操作可能会因为没有后续事件而一直处于阻塞状态
 */
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
/* 将fd上的EPOLLIN注册到epfd指示的epoll内核事件表中，
 * one_shoot指示是否注册EPOLLONESHOT
 * 注册EPOLLONESHOT后，一个socket连接在任一时刻只被一个线程处理
 */
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
  
#ifdef connfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listenfdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//从内核事件表删除描述符
void removedfd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;

#ifdef connfdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);

}

int http_conn::m_user_cout = 0;
int http_conn::m_epollfd = -1;

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close) {
    if(real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态
void http_conn::init() {
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_read_buf, '\0', FILENAME_LEN);
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
/*从状态机 
    GET / HTTP/1.1\r\nHost: www.sina.com.cn\r\nConnection: close\r\n\r\n
*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;

    for(; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_check_idx];

        //http的换行回车是window格式的\r\n
        if(temp == '\r') {
            if((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            //截断字符串,已经读取了一行
            else if(m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;    //其他的情况就是error了
        }
        else if (temp == '\n') {
            //上一个是\r(>1表示一开始不可能就是\r\n
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                reutn LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;//继续读取,此次读取的数据已经解析完毕
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
// nonblock + et 需要一次性读取完数据
bool http_conn::read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;

#ifdef connfdLT //LT模式

    bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if(bytes_read <= 0) {
        return false;
    }

    return true;

#endif

}

// 解析请求行，也就是GET中的GET /562f25980001b1b106000338.jpg HTTP/1.1这一行，或者POST中的POST / HTTP1.1这一行。
// 通过请求行的解析我们可以判断该HTTP请求的类型（GET/POST），而请求行中最重要的部分就是URL部分，我们会将这部分保存下来用于后面的生成HTTP响应。
//解析http请求行，获得请求方法，目标url及http版本号
/*主状态机,解析http请求行
 GET / HTTP/1.1\r\nHost: www.sina.com.cn\r\nConnection: close\r\n\r\n
*/
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    // 检索字符串 text 中第一个匹配字符串 " \t" 中字符的字符
    m_url = strpbrk(text, " \t");
    if(!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char *method = text;

    if(strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    }
    else
        return BAD_REQUEST;
    
    // 计算字符串 m_url 中连续有几个字符都属于字符串 " \t"
    // 跳过可能有的空格
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ '\0';  //空格变回车
    m_version += strspn(m_version, " \t");
    if(strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;     //非1.1版本则error

    // 比较 url 的 前七个字符是否相等
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/'); // 第一次出现 '/' 的 位置
    }

    // https连接
    if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    // 当 url 为，显示判断页面
    if(strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析请求头部，GET和POST中空行以上，请求行以下的部分
//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    if(text[0] == '\0') {
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;    //状态转移:请求头部->请求content
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");

        if(strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");    // 移动到下一行
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        LOG_INFO("oop!unknow head: %s", text);
        Log::get_instance()->flush();
    }
    return NO_REQUEST;
}

// 解析请求数据，对于GET来说这部分是空的，因为这部分内容已经以明文的方式包含在了请求行中的URL部分了；
// 只有POST的这部分是有数据的，项目中的这部分数据为用户名和密码，我们会根据这部分内容做登录和校验，并涉及到与数据库的连接
// 判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if(m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';

        //POST请求中最后为输入的用户名和密码
        m_string = text;    // 拿到的用户名和密码
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 处理整个http请求
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status == parse_line()) == LINE_OK)) {
        text = getline();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        Log::get_instance()->flush();

        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                // 解析请求行，也就是GET中的`GET /562f25980001b1b106000338.jpg HTTP/1.1这一行，或者POST中的`POST / HTTP1.1这一行
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                return BAD_REQUEST;
                break;
            }

            case CHECK_STATE_HEADER: {
                // 解析请求头部
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }

            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                    return do_request();
                line_status = LINE_OPEN;    //内容没有完整拿到
                break;
            }

            default:
                return INTERNAL_ERROR;
        }
    }
    //LINE_OPEN:数据为接收完毕则while退出
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    // p指向 '/'最后一次出现的位置
    //拿到url中最后一个/开头的地址
    const char *p = strrchr(m_url, '/');

    // 处理cgi 2cgi是login功能,3cgi是register功能
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3')) {
        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        for(i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        int j = 0;
        for(i = i + 10; m_string[i] != '\0'; ++i, ++j) {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        //同步线程登录校验
        if(*(p + 1) == 3) {     // 注册
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            // 没有找到重名
            if(users.find(name) == users.end()) {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if(!res)
                    strcpy(m_url, "/log.html");
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }

        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        // 注册校验
        else if(*(p + 1) == '2') {
            // 校验成功
            if(users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            // 校验失败
            else
                strcpy(m_url, "/logError.html");
        }
    }

    // 注册页面
    if(*(p + 1) == '0') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcap(m_url_real, "/register.html");
        strncap(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // 登陆页面
    else if(*(p + 1) == '1') {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    // 图片页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

     // 视频页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    // 关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }

    else
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    
    //获取文件状态
    if(stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    
    //文件权限不够
    if(!(m_file_stat.st_mode && S_IROTH))
        return FORBIDDEN_REQUEST;
    
    //请求的是文件夹
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    //将磁盘文件映射到内存中
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

//取消html文件的映射
void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st.size);
    }
}

//最后返回给客户端的调用的函数(发给客户端的Http包组装好了)
void http_conn::write() {
    int temp = 0;

    //没有内容需要发送给客户端就改为监听状态
    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    //发送给客户端
    while(1) {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if(temp < 0) {
            if(errno == EAGAIN) {
                //继续注册写事件,缓冲区的内容没发送完
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            //其他的错误
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;

        if(bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }


        if(bytes_to_send <= 0) {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            // 保持连接
            if(m_linger) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}

/*将http response包的内容写入到m_write_buf中
 * m_write_idx记m_iv[0]的长度
*/
bool http_conn::add_response(const char *format, ...) {
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);

    //WRITE_BUFFER_SIZE-1-m_write_idx是建议值,当后面str长度大于它则无效
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);

    if(len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        va_end(arg_list);
        return false;
    }

    m_write_idx += len;
    va_end(arg_list);
    LOG_INFO("request: %s", m_write_buf);
    Log::get_instance()->flush();
    return true;
}

//返回包的状态行
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

//包头
bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

//是否保持连接?
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

//由do_request()函数转移过来
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return false;
            break;
        }
        case BAD_REQUEST: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return ;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLOUT);
}