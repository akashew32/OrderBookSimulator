#include "csv_files.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

CsvFileCatalog::CsvFileCatalog(std::string data_folder)
    : data_folder_(std::move(data_folder)) {}

const std::string& CsvFileCatalog::data_folder() const {
    return data_folder_;
}

std::vector<CsvMetadata> CsvFileCatalog::list(std::string& error) const {
    std::vector<CsvMetadata> files;
    error.clear();

    if (!fs::exists(data_folder_)) {
        error = "Replay data folder does not exist: " + data_folder_;
        return files;
    }

    for (const auto& entry : fs::recursive_directory_iterator(data_folder_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".csv") {
            continue;
        }

        CsvMetadata metadata;
        std::string inspect_error;
        if (CsvReplayEngine::inspect(entry.path().string(), metadata, inspect_error)) {
            files.push_back(metadata);
        }
    }

    std::sort(files.begin(), files.end(), [](const CsvMetadata& a, const CsvMetadata& b) {
        return a.path < b.path;
    });
    return files;
}

bool CsvFileCatalog::inspect(const std::string& path, CsvMetadata& metadata, std::string& error) const {
    if (!is_allowed_csv_path(path)) {
        error = "CSV path must stay inside " + data_folder_ + " and end in .csv";
        return false;
    }
    return CsvReplayEngine::inspect(path, metadata, error);
}

bool CsvFileCatalog::save_upload(const std::string& file_name,
                                 const std::string& content,
                                 CsvMetadata& metadata,
                                 std::string& error) const {
    std::string path = safe_upload_path(file_name, error);
    if (path.empty()) {
        return false;
    }

    fs::create_directories(fs::path(path).parent_path());
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            error = "Could not write upload to " + path;
            return false;
        }
        out << content;
    }

    if (!inspect(path, metadata, error)) {
        fs::remove(path);
        return false;
    }
    return true;
}

std::string CsvFileCatalog::safe_upload_path(const std::string& file_name, std::string& error) const {
    fs::path name = fs::path(file_name).filename();
    if (name.empty() || name.extension() != ".csv") {
        error = "Uploaded file must have a .csv extension";
        return "";
    }

    std::string clean = name.string();
    for (char& ch : clean) {
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '_' || ch == '-')) {
            ch = '_';
        }
    }
    return (fs::path(data_folder_) / "uploads" / clean).string();
}

bool CsvFileCatalog::is_allowed_csv_path(const std::string& path) const {
    fs::path input(path);
    if (input.extension() != ".csv") {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    fs::path folder = fs::weakly_canonical(data_folder_);
    fs::path full = fs::weakly_canonical(input);
    auto folder_text = folder.string();
    auto full_text = full.string();
    return full_text.rfind(folder_text, 0) == 0;
}
