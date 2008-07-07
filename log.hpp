
#ifndef _LOG_HPP_
#define _LOG_HPP_

#include <windows.h>
#include <tchar.h>
#include <stdio.h>


class File
{
public:
    File(const TCHAR *szName, const TCHAR *szExtension) {
        m_hFile = INVALID_HANDLE_VALUE;
        Open(szName, szExtension);
    }
    
    ~File() {
        Close();
    }

    void Open(const TCHAR *szName, const TCHAR *szExtension) {
        Close();
        
        DWORD dwCounter = 0;
        do {
            if(dwCounter)
                _sntprintf(szFileName, MAX_PATH, TEXT("%s.%u.%s"), szName, dwCounter, szExtension);
            else
                _sntprintf(szFileName, MAX_PATH, TEXT("%s.%s"), szName, szExtension);

            m_hFile = CreateFile(szFileName,
                                 GENERIC_WRITE,
                                 FILE_SHARE_WRITE,
                                 NULL,
                                 CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);
            ++dwCounter;
        } while(m_hFile == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS);
    }
    
    void ReOpen(void) {
        Close();
        
        m_hFile = CreateFile(szFileName,
                             GENERIC_WRITE,
                             0,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    }
    
    void Close(void) {
        if(m_hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hFile);
            m_hFile = INVALID_HANDLE_VALUE;
        }
    }
    
    void Write(const char *szText) {
        if(m_hFile == INVALID_HANDLE_VALUE)
            return;
        
        DWORD dwBytesToWrite = (DWORD)strlen(szText);
        DWORD dwBytesWritten = 0;
        
        while (dwBytesWritten < dwBytesToWrite) {
            OVERLAPPED overlapped;
            memset(&overlapped, 0, sizeof(OVERLAPPED));

            /* Write to end of file */
            overlapped.Offset = 0xffffffff;
            overlapped.OffsetHigh = 0xffffffff;
            
            if(WriteFile(m_hFile,
                         szText + dwBytesWritten,
                         dwBytesToWrite - dwBytesWritten,
                         &dwBytesWritten,
                         &overlapped) == FALSE) {
                Close();
                Open(TEXT("extra"), TEXT("xml"));
                return;
            }
        }
    }
    
private:
    HANDLE m_hFile;
    TCHAR szFileName[MAX_PATH];
};


class Log : public File
{
public:
    Log(const TCHAR *szName) : File(szName, TEXT("xml")) {
        Write("<?xml version='1.0' encoding='UTF-8'?>");
        NewLine();
        Write("<?xml-stylesheet type='text/xsl' href='d3dtrace.xsl'?>");
        NewLine();
        Write("<trace>");
        NewLine();
    }
    
    ~Log() {
        Write("</trace>");
        NewLine();
    }
    
    void NewLine(void) {
        Write("\r\n");
    }
    
    void Tag(const char *name) {
        Write("<");
        Write(name);
        Write("/>");
    }
    
    void BeginTag(const char *name) {
        Write("<");
        Write(name);
        Write(">");
    }
    
    void BeginTag(const char *name, 
                  const char *attr1, const char *value1) {
        Write("<");
        Write(name);
        Write(" ");
        Write(attr1);
        Write("=\"");
        Escape(value1);
        Write("\">");
    }
    
    void BeginTag(const char *name, 
                  const char *attr1, const char *value1,
                  const char *attr2, const char *value2) {
        Write("<");
        Write(name);
        Write(" ");
        Write(attr1);
        Write("=\"");
        Escape(value1);
        Write("\" ");
        Write(attr2);
        Write("=\"");
        Escape(value2);
        Write("\">");
    }
    
    void EndTag(const char *name) {
        Write("</");
        Write(name);
        Write(">");
    }
    
    void Text(const char *text) {
        Escape(text);
    }
    
    void TextF(const char *format, ...) {
        char szBuffer[4196];
        va_list ap;
        va_start(ap, format);
        vsnprintf(szBuffer, sizeof(szBuffer), format, ap);
        va_end(ap);
        Escape(szBuffer);
    }
    
    void BeginCall(const char *function) {
        Write("\t");
        BeginTag("call", "name", function);
        NewLine();
    }
    
    void EndCall(void) {
        Write("\t");
        EndTag("call");
        NewLine();
    }
    
    void BeginParam(const char *name, const char *type) {
        Write("\t\t");
        BeginTag("param", "name", name, "type", type);
    }
    
    void EndParam(void) {
        EndTag("param");
        NewLine();
    }
    
    void BeginReturn(const char *type) {
        Write("\t\t");
        BeginTag("return", "type", type);
    }
    
    void EndReturn(void) {
        EndTag("return");
        NewLine();
    }
    
protected:
    void Escape(const char *s) {
        /* FIXME */
        Write(s);
    }
};


extern Log * g_pLog;


#endif /* _LOG_HPP_ */