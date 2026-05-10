#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>

class FileIO {
public:
    explicit FileIO(const std::filesystem::path& path) : path_(path) {}
    ~FileIO() {
        Close();
    }

    bool Exists() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::error_code ec;
        return std::filesystem::exists(path_, ec);
    }

    bool Open(bool create = true) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) return true;

        if (create) {
            std::error_code ec;
            const bool pathExists = std::filesystem::exists(path_, ec);
            if (ec) {
                return false;
            }

            if (!pathExists) {
                const auto parent = path_.parent_path();
                if (!parent.empty()) {
                    std::filesystem::create_directories(parent, ec);
                    if (ec) {
                        return false;
                    }
                }

                std::ofstream tmp(path_, std::ios::binary);
                if (!tmp.is_open()) {
                    return false;
                }
            }
        }

        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
        return file_.is_open();
    }

    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    void SeekTo(std::streampos position) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.seekg(position);
            file_.seekp(position);
        }
    }

    void Write(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.write(data.data(), static_cast<std::streamsize>(data.size()));
            file_.flush();
        }
    }

    void Append(const std::string& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.seekp(0, std::ios::end);
            file_.write(data.data(), static_cast<std::streamsize>(data.size()));
            file_.flush();
        }
    }

    template <typename... Args>
    void Append(std::format_string<Args...> fmt, Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.seekp(0, std::ios::end);
            auto data = std::format(fmt, std::forward<Args>(args)...);
            file_.write(data.data(), static_cast<std::streamsize>(data.size()));
            file_.flush();
        }
    }

    void Read(std::string& outData, std::streamsize size) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            outData.resize(static_cast<std::size_t>(size));
            file_.read(outData.data(), size);
        }
    }

    void ReadAll(std::string& outData) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.seekg(0, std::ios::beg);
            outData.assign((std::istreambuf_iterator<char>(file_)),
                            std::istreambuf_iterator<char>());
        }
    }

    ssize_t Size() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            auto currentPos = file_.tellg();
            file_.seekg(0, std::ios::end);
            auto size = file_.tellg();
            file_.seekg(currentPos);
            return static_cast<ssize_t>(size);
        }
        return -1;
    }

    void Flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
    }

    void Delete() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }

        std::error_code ec;
        if (std::filesystem::exists(path_, ec)) {
            std::filesystem::remove(path_, ec);
        }
    }

    void Rename(const std::filesystem::path& newPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }

        std::error_code ec;
        std::filesystem::rename(path_, newPath, ec);
        if (ec) {
            return;
        }

        path_ = newPath;
        file_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
    }

    void SetPath(const std::filesystem::path& newPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        path_ = newPath;
    }

    struct BatchOps {
        std::fstream& file_;
        void Write(const std::string& data) {
            if (file_.is_open()) { file_.write(data.data(), static_cast<std::streamsize>(data.size())); file_.flush(); }
        }
        void Append(const std::string& data) {
            if (file_.is_open()) { file_.seekp(0, std::ios::end); file_.write(data.data(), static_cast<std::streamsize>(data.size())); file_.flush(); }
        }
        template <typename... Args>
        void Append(std::format_string<Args...> fmt, Args&&... args) {
            if (file_.is_open()) {
                file_.seekp(0, std::ios::end);
                auto data = std::format(fmt, std::forward<Args>(args)...);
                file_.write(data.data(), static_cast<std::streamsize>(data.size()));
                file_.flush();
            }
        }
        void SeekTo(std::streampos pos) {
            if (file_.is_open()) { file_.seekg(pos); file_.seekp(pos); }
        }
        void Flush() { if (file_.is_open()) file_.flush(); }
    };

    template<typename Fn>
    void batch(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        fn(BatchOps{file_});
    }

private:
    mutable std::mutex mutex_;
    std::fstream file_;
    std::filesystem::path path_;
};
