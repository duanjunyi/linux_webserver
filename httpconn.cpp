#include "httpconn.h"


// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";


// 网站的根目录
const char* doc_root = "/mnt/D/Program/webserver/myProgram/resources";


//关闭连接
void httpconn::close_conn()
{
    if(m_sockfd != -1)
    {
        m_epollObj.removefd(m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}


//初始化连接，外部调用初始化套接字地址
void httpconn::init(int sockfd, const sockaddr_in& addr)
{
    m_sockfd = sockfd;
    m_address = addr;

    //端口复用
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    m_epollObj.addfd(m_sockfd, true);
    m_user_count++;
    init();
}


//默认构造函数
void httpconn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;// 初始状态为检查请求行
    m_linger = false;                       // 默认不保持链接  Connection : keep-alive保持连接
    m_method = GET;                         // 默认请求方式为GET
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_bytes_to_send = 0;
    m_bytes_have_send = 0;
    bzero(m_read_buf, READ_BUFFER_SIZE);
    bzero(m_write_buf, WRITE_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}


//循环读取客户数据，直到无数据可读或者对方关闭连接
bool httpconn::read()
{
    if(m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while(true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if(bytes_read == -1)
        {
            //没有数据
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            //被打断
            else if(errno == EINTR)
                continue;
            else
                return false;
        }
        //对方关闭连接
        else if(bytes_read == 0)
            return false;
        else
            m_read_idx += bytes_read;
    }
    return true;
}


//由线程池的工作线程调用，这是处理HTTP请求的入口函数
void httpconn::process()
{
    //解析HTTP请求
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST)
    {
        m_epollObj.modfd(m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret)
    {
        close_conn();
    }
    m_epollObj.modfd(m_sockfd, EPOLLOUT);
}


//主状态机，解析请求
httpconn::HTTP_CODE httpconn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    /*
    m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK 表示当前状态为解析请求体且上一次解析正常
    (line_status = parse_line()) == LINE_OK 表示请求行或者请求头部解析正确
    */
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
    || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line: %s\n", text);


        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST)
                    return BAD_REQUEST;
                else if(ret == GET_REQUEST)
                    return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret = parse_content(text);
                if(ret == GET_REQUEST)
                    return do_request();
                else
                    line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}


// 解析一行，判断依据\r\n
httpconn::LINE_STATUS httpconn::parse_line()
{
    char temp;
    for(;m_checked_idx < m_read_idx;m_checked_idx++)
    {
        temp = m_read_buf[m_checked_idx];
        //回车符
        if(temp == '\r')
        {
            if((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if(m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '0';
                m_read_buf[m_checked_idx++] = '0';
                return LINE_OK;
            }
            else
                return LINE_BAD;
        }
        else if(temp == '\n')
        {
            if(m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == 'r')
            {
                m_read_buf[m_checked_idx - 1] = '0';
                m_read_buf[m_checked_idx++] = '0';
                return LINE_OK;
            }
            else
                return LINE_BAD;
        }
    }
    return LINE_OPEN;
}


// 解析HTTP请求行，获得请求方法，目标URL,以及HTTP版本号
httpconn::HTTP_CODE httpconn::parse_request_line(char* text)
{
    // GET /index.html HTTP/1.1
    m_url = strpbrk(text, " \t"); // 判断第二个参数中的字符哪个在text中最先出现
    if(!m_url)
        return BAD_REQUEST;

    // GET\0/index.html HTTP/1.1
    *m_url++ = '\0';    // 置位空字符，字符串结束符
    char* method = text;
    if(strcasecmp(method, "GET") == 0)  // 忽略大小写比较
        m_method = GET;
    else
        return BAD_REQUEST;

    // /index.html HTTP/1.1
    // 检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    m_version = strpbrk(m_url, " \t");
    if(!m_version)
        return BAD_REQUEST;

    // /index.html\0HTTP/1.1
    *m_version++ = '\0';
    if(strcasecmp(m_version, "HTTP") != 0)
        return BAD_REQUEST;

    //http://192.168.110.129:10000/index.html
    if(strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        // 在参数 str 所指向的字符串中搜索第一次出现字符 c（一个无符号字符）的位置
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER; //检查状态变成检查头
    return NO_REQUEST;
}


//解析HTTP请求的一个头部信息
httpconn::HTTP_CODE httpconn::parse_headers(char* text)
{
    // 遇到空行，表示头部字段解析完毕
    if(text == '\0')
    {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体，
        // 状态机转移到CHECK_STATE_CONTENT状态
        if(m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明我们已经得到了一个完整的HTTP请求
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0)
    {
        // 处理Connection 头部字段  Connection: keep-alive
        text += 11;
        text += strspn(text, " \t");
        if(strcasecmp( text, "keep-alive") == 0)
            m_linger = true;
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0)
    {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0)
    {
        // 处理Host头部字段
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
        printf("oop! unknowen header %s\n", text);
    return NO_REQUEST;
}


// 我们没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
httpconn::HTTP_CODE httpconn::parse_content(char* text)
{
    if(m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        printf("Get Content: %s\n", text);
        return GET_REQUEST;
    }
    return NO_REQUEST;
}


// 当得到一个完整、正确的HTTP请求时，我们就分析目标文件的属性，
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其
// 映射到内存地址m_file_address处，并告诉调用者获取文件成功
httpconn::HTTP_CODE httpconn::do_request()
{
    // "/home/nowcoder/webserver/resources"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取m_real_file文件的相关的状态信息，-1失败，0成功
    if(stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;

    // 判断访问权限
    if(!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;

    // 判断是否是目录
    if(S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;

    //以只读的方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    //创建内存映射
    m_file_address = (char * )mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}


//对内存映射区进行munmap操作
bool httpconn::unmap()
{
    if(m_file_address)
    {
        int ret = munmap(m_file_address, m_file_stat.st_size);
        if(ret == -1)
        {
            perror("munmap");
            return false;
        }
        m_file_address = 0;
    }
    return true;
}


//写HTTP响应
bool httpconn::write()
{
    int temp = 0;

    if(m_bytes_to_send == 0)
    {
        // 将要发送的字节为0，这一次响应结束
        m_epollObj.modfd(m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1)
    {
        //分散写
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if(temp <= -1)
        {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN)
            {
                m_epollObj.modfd(m_sockfd, EPOLLOUT);
                return true;
            }
            else
            {
                unmap();
                return false;
            }
        }
        m_bytes_to_send -= temp;
        m_bytes_have_send += temp;
        if(m_bytes_have_send >= m_iv[0].iov_len)
        {
            //头已经发送完毕
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (m_bytes_have_send - m_write_idx);
            m_iv[1].iov_len = m_bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + m_bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - temp;
        }

        if(m_bytes_to_send <= 0)
        {
            //没有数据要发送
            unmap();
            m_epollObj.modfd(m_sockfd, EPOLLIN);
            if(m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}


// 往写缓冲中写入待发送的数据
bool httpconn::add_response(const char* format, ...)
{
    if(m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if(len >= WRITE_BUFFER_SIZE - 1 - m_write_idx)
        return false;
    m_write_idx += len;
    va_end(arg_list);
    return true;
}


bool httpconn::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}


bool httpconn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_content_type();
    add_linger();
    add_blank_line();
}


bool httpconn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}

bool httpconn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}


bool httpconn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}


bool httpconn::add_blank_line()
{
    return add_response("%s", "\r\n");
}


bool httpconn::add_content(const char* content)
{
    return add_response("%s", content);
}


//根据服务器处理HTTP请求的结果，决定返回客户端的内容
bool httpconn::process_write(HTTP_CODE ret)
{
    switch(ret)
    {
        case INTERNAL_ERROR:
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form))
                return false;
            break;
        case BAD_REQUEST:
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form))
                return false;
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            m_bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        default:
            return true;
    }

    //响应报文响应头
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    m_bytes_to_send = m_write_idx;
    return true;
}

