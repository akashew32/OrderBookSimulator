#pragma once

#include <string>
#include <vector>

#include "csv_replay.hpp"

class CsvFileCatalog {
public:
    explicit CsvFileCatalog(std::string data_folder = "data");

    const std::string& data_folder() const;
    std::vector<CsvMetadata> list(std::string& error) const;
    bool inspect(const std::string& path, CsvMetadata& metadata, std::string& error) const;
    bool save_upload(const std::string& file_name,
                     const std::string& content,
                     CsvMetadata& metadata,
                     std::string& error) const;

private:
    std::string data_folder_;

    std::string safe_upload_path(const std::string& file_name, std::string& error) const;
    bool is_allowed_csv_path(const std::string& path) const;
};
