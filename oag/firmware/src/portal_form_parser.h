#pragma once
inline bool formValue(const char* body,const char* key,char* out,std::size_t cap) {
    if(!body||!key||!out||cap==0)return false; out[0]='\0'; const std::size_t kl=std::strlen(key);
    const char* c=body;
    while(*c){
        if((c==body||c[-1]=='&')&&std::strncmp(c,key,kl)==0&&c[kl]=='='){
            c+=kl+1; std::size_t w=0;
            while(*c&&*c!='&'){
                char ch=*c++;
                if(ch=='+')ch=' ';
                if(ch=='%'&&c[0]&&c[1]){
                    auto hx=[](char h)->int{if(h>='0'&&h<='9')return h-'0';if(h>='a'&&h<='f')return h-'a'+10;if(h>='A'&&h<='F')return h-'A'+10;return -1;};
                    int hi=hx(c[0]),lo=hx(c[1]); if(hi<0||lo<0)return false; ch=static_cast<char>((hi<<4)|lo);c+=2;
                }
                if(ch=='\0'||ch=='\r'||ch=='\n'||w+1>=cap)return false; out[w++]=ch;
            }
            out[w]='\0';return true;
        }
        const char* n=std::strchr(c,'&');if(!n)break;c=n+1;
    }
    return false;
}

inline bool parseUnsigned(const char* b,const char* k,std::uint32_t lo,std::uint32_t hi,std::uint32_t& v){
    char x[16]{};if(!formValue(b,k,x,sizeof(x))||!x[0])return false;char* e=nullptr;unsigned long p=std::strtoul(x,&e,10);
    if(e==x||*e||p<lo||p>hi)return false;v=static_cast<std::uint32_t>(p);return true;
}
inline bool parseSigned(const char* b,const char* k,std::int32_t lo,std::int32_t hi,std::int32_t& v){
    char x[16]{};if(!formValue(b,k,x,sizeof(x))||!x[0])return false;char* e=nullptr;long p=std::strtol(x,&e,10);
    if(e==x||*e||p<lo||p>hi)return false;v=static_cast<std::int32_t>(p);return true;
}

