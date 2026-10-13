#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <sys/stat.h>

namespace ttnx::library {
// Strict and bounded local-only library. No Google credentials, URLs,
// thumbnails, videos, signed CDN parameters, or arbitrary server strings.
struct Entry {
    std::string video_id;
    std::uint64_t last_watched{0};
    std::uint32_t resume_seconds{0};
};
class LocalLibrary final {
public:
    static constexpr std::size_t kHistoryMax=1000;
    static constexpr std::size_t kWatchLaterMax=1000;
    static constexpr std::size_t kFavoritesMax=400;
    static constexpr std::size_t kFollowingMax=100;
    static constexpr std::size_t kFileMaxBytes=65536;
    // Primary, temporary and previous together stay below 2 MB.
    static constexpr std::size_t kTotalDiskMaxBytes=3*kFileMaxBytes;
    [[nodiscard]] static bool valid_id(std::string_view id) noexcept {
        if(id.size()!=11)return false;
        for(const unsigned char c:id)
            if(!(c>='a'&&c<='z') && !(c>='A'&&c<='Z') &&
               !(c>='0'&&c<='9') && c!='_' && c!='-')return false;
        return true;
    }
    [[nodiscard]] static bool valid_channel_id(std::string_view id) noexcept {
        if(id.size()!=24 || id.substr(0,2)!="UC")return false;
        for(const unsigned char c:id.substr(2))
            if(!(c>='a'&&c<='z') && !(c>='A'&&c<='Z') &&
               !(c>='0'&&c<='9') && c!='_' && c!='-')return false;
        return true;
    }
    void record_watch(std::string_view id,std::uint64_t when,std::uint32_t seconds=0) {
        if(!history_enabled_ || !valid_id(id))return;
        auto it=std::find_if(history_.begin(),history_.end(),[&](const Entry& e){return e.video_id==id;});
        if(it!=history_.end())history_.erase(it);
        history_.insert(history_.begin(),Entry{std::string(id),when,seconds});
        if(history_.size()>kHistoryMax)history_.resize(kHistoryMax);
    }
    void set_history_enabled(bool yes) noexcept {history_enabled_=yes;}
    [[nodiscard]] bool history_enabled()const noexcept{return history_enabled_;}
    void clear_history() noexcept {history_.clear();}
    [[nodiscard]] bool erase_history(std::string_view id) {
        const auto n=history_.size();
        std::erase_if(history_,[&](const Entry& e){return e.video_id==id;});
        return n!=history_.size();
    }
    [[nodiscard]] bool add_watch_later(std::string_view id) {
        if(!valid_id(id))return false;
        if(std::find(later_.begin(),later_.end(),id)!=later_.end())return true;
        if(later_.size()>=kWatchLaterMax)return false;
        later_.emplace_back(id);return true;
    }
    [[nodiscard]] bool remove_watch_later(std::string_view id) {
        const auto n=later_.size();
        std::erase(later_,std::string(id));return later_.size()!=n;
    }
    [[nodiscard]] bool add_favorite(std::string_view id) {
        return add_unique(favorites_,id,kFavoritesMax,false);
    }
    [[nodiscard]] bool remove_favorite(std::string_view id) {
        return remove_id(favorites_,id);
    }
    [[nodiscard]] bool follow_channel(std::string_view id) {
        return add_unique(followed_,id,kFollowingMax,true);
    }
    [[nodiscard]] bool unfollow_channel(std::string_view id) {
        return remove_id(followed_,id);
    }
    [[nodiscard]] const std::vector<std::string>& favorites()const noexcept{return favorites_;}
    [[nodiscard]] const std::vector<std::string>& followed_channels()const noexcept{return followed_;}
    [[nodiscard]] const std::vector<Entry>& history()const noexcept{return history_;}
    [[nodiscard]] const std::vector<std::string>& watch_later()const noexcept{return later_;}
    [[nodiscard]] std::string serialize()const {
        std::string result="TTNX-LIB-2\n";
        result += history_enabled_?"1\n":"0\n";
        result += std::to_string(history_.size())+"\n";
        for(const auto& e:history_)
            result+=e.video_id+" "+std::to_string(e.last_watched)+" "+
                    std::to_string(e.resume_seconds)+"\n";
        result += std::to_string(later_.size())+"\n";
        for(const auto& id:later_)result+=id+"\n";
        result+=std::to_string(favorites_.size())+"\n";
        for(const auto& id:favorites_)result+=id+"\n";
        result+=std::to_string(followed_.size())+"\n";
        for(const auto& id:followed_)result+=id+"\n";
        if(result.size()+18>kFileMaxBytes)return {};
        std::uint32_t sum=2166136261u;
        for(unsigned char c:result)sum=(sum^c)*16777619u;
        char suffix[20]{};
        std::snprintf(suffix,sizeof(suffix),"%08x\n",sum);
        result+=suffix;
        return result;
    }
    [[nodiscard]] static bool parse(std::string_view text,LocalLibrary& output) {
        if(text.empty() || text.size()>kFileMaxBytes)return false;
        const auto last=text.rfind('\n',text.size()-2);
        if(last==std::string_view::npos || text.back()!='\n' ||
           text.size()-last!=10)return false;
        std::uint32_t checksum=2166136261u;
        const auto prefix=text.substr(0,last+1);
        for(unsigned char c:prefix)checksum=(checksum^c)*16777619u;
        char suffix[20]{};std::snprintf(suffix,sizeof(suffix),"%08x\n",checksum);
        if(text.substr(last+1)!=suffix)return false;
        std::size_t at=0;
        const auto line=[&](std::string_view& value)->bool {
            const auto end=prefix.find('\n',at);
            if(end==std::string_view::npos)return false;
            value=prefix.substr(at,end-at);at=end+1;return true;
        };
        const auto number=[](std::string_view value,std::uint64_t& out)->bool {
            if(value.empty()||value.size()>20||
               (value.size()>1&&value.front()=='0'))return false;
            std::uint64_t n=0;
            for(char c:value) {
                if(c<'0'||c>'9'||n>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10)return false;
                n=n*10+static_cast<unsigned>(c-'0');
            }
            out=n;return true;
        };
        LocalLibrary staged;
        std::string_view value;
        if(!line(value) || (value!="TTNX-LIB-1"&&value!="TTNX-LIB-2"))return false;
        const bool version_two=value=="TTNX-LIB-2";
        if(!line(value)||(value!="1"&&value!="0"))return false;
        staged.history_enabled_=value=="1";
        if(!line(value))return false;
        std::uint64_t count=0;
        if(!number(value,count)||count>kHistoryMax)return false;
        for(std::uint64_t i=0;i<count;++i) {
            if(!line(value))return false;
            const auto a=value.find(' '),b=value.find(' ',a==std::string_view::npos?0:a+1);
            if(a!=11||b==std::string_view::npos||
               !valid_id(value.substr(0,a)))return false;
            std::uint64_t when=0,resume=0;
            if(!number(value.substr(a+1,b-a-1),when)||
               !number(value.substr(b+1),resume)||resume>UINT32_MAX)return false;
            const auto id=value.substr(0,a);
            if(std::find_if(staged.history_.begin(),staged.history_.end(),
                [&](const Entry& e){return e.video_id==id;})!=staged.history_.end())return false;
            staged.history_.push_back({std::string(id),when,static_cast<std::uint32_t>(resume)});
        }
        if(!line(value)||!number(value,count)||count>kWatchLaterMax)return false;
        for(std::uint64_t i=0;i<count;++i) {
            if(!line(value)||!staged.add_watch_later(value))return false;
            // Duplicated IDs must not produce a different count.
            if(staged.later_.size()!=i+1)return false;
        }
        if(version_two) {
            if(!line(value)||!number(value,count)||count>kFavoritesMax)return false;
            for(std::uint64_t i=0;i<count;++i) {
                if(!line(value)||!staged.add_favorite(value) ||
                   staged.favorites_.size()!=i+1)return false;
            }
            if(!line(value)||!number(value,count)||count>kFollowingMax)return false;
            for(std::uint64_t i=0;i<count;++i) {
                if(!line(value)||!staged.follow_channel(value) ||
                   staged.followed_.size()!=i+1)return false;
            }
        }
        if(at!=prefix.size())return false;
        output=std::move(staged);return true;
    }
    // App-owned path base, caller supplies sdmc:/switch/TizenTube-NX/library.
    // Symlinks, unknown file types and overlong files are never read/written.
    [[nodiscard]] bool load(const std::string& base) {
        LocalLibrary candidate;
        if(read_file(base,candidate)||read_file(base+".bak",candidate)) {
            *this=std::move(candidate);return true;
        }
        return false;
    }
    [[nodiscard]] bool save(const std::string& base)const {
        const auto data=serialize();
        if(data.empty()||!file_safe(base)||!file_safe(base+".tmp")||
           !file_safe(base+".bak"))return false;
        const auto temp=base+".tmp",backup=base+".bak";
        if(std::remove(temp.c_str())!=0 && !missing(temp))return false;
        FILE* f=std::fopen(temp.c_str(),"wb");
        if(!f)return false;
        const bool ok=std::fwrite(data.data(),1,data.size(),f)==data.size() &&
                      std::fflush(f)==0;
        const bool closed=std::fclose(f)==0;
        if(!ok||!closed)return false;
        LocalLibrary verified;
        if(!read_file(temp,verified))return false;
        const bool original=exists_regular(base);
        LocalLibrary good_previous;
        const bool primary_valid=original && read_file(base,good_previous);
        if(original && primary_valid) {
            if(std::remove(backup.c_str())!=0 && !missing(backup))return false;
            if(std::rename(base.c_str(),backup.c_str())!=0)return false;
        } else if(original) {
            // Recover from an invalid primary only if a valid backup remains.
            // Never replace the sole restorable local Library with corruption.
            if(!read_file(backup,good_previous) || std::remove(base.c_str())!=0)
                return false;
        }
        if(std::rename(temp.c_str(),base.c_str())!=0) {
            if(original && primary_valid)
                (void)std::rename(backup.c_str(),base.c_str());
            return false;
        }
        return true;
    }
private:
    static bool remove_id(std::vector<std::string>& list,std::string_view id) {
        const auto n=list.size();std::erase(list,std::string(id));return n!=list.size();
    }
    static bool add_unique(std::vector<std::string>& list,std::string_view id,
                           std::size_t max,bool channel) {
        if(channel?!valid_channel_id(id):!valid_id(id))return false;
        if(std::find(list.begin(),list.end(),id)!=list.end())return true;
        if(list.size()>=max)return false;
        list.emplace_back(id);return true;
    }
    static bool missing(const std::string& path) {
        struct stat st{};return lstat(path.c_str(),&st)!=0;
    }
    static bool file_safe(const std::string& path) {
        struct stat st{};
        return lstat(path.c_str(),&st)!=0 ||
            (S_ISREG(st.st_mode)&&st.st_size>=0&&st.st_size<=static_cast<off_t>(kFileMaxBytes));
    }
    static bool exists_regular(const std::string& path) {
        struct stat st{};return lstat(path.c_str(),&st)==0&&S_ISREG(st.st_mode);
    }
    static bool read_file(const std::string& path,LocalLibrary& out) {
        if(!exists_regular(path))return false;
        FILE* f=std::fopen(path.c_str(),"rb");
        if(!f)return false;
        std::array<char,kFileMaxBytes+1> bytes{};
        const auto size=std::fread(bytes.data(),1,bytes.size(),f);
        const bool io_good=!std::ferror(f);
        const bool closed=std::fclose(f)==0;
        const bool ok=io_good&&closed;
        return ok && size<=kFileMaxBytes && parse({bytes.data(),size},out);
    }
    bool history_enabled_{true};
    std::vector<Entry> history_{};
    std::vector<std::string> later_{};
    std::vector<std::string> favorites_{};
    std::vector<std::string> followed_{};
};
} // namespace ttnx::library
