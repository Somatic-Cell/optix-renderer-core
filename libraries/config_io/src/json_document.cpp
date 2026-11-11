#include <renderer/config_io/json_document.hpp>

#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace renderer::config_io
{

namespace 
{

std::string path_text(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}


void require_path(const std::filesystem::path& path)
{
    if (path.empty())
        throw std::invalid_argument("Configuration path must not be empty");

    // 文字列に \0 が含まれていないかを調べる
    if (
        path.native().find(std::filesystem::path::value_type{})     // value_type{} は \0. path の文字列から探す 
        != std::filesystem::path::string_type::npos                 // 文字列の検索に失敗
    ){
        throw std::invalid_argument("Configuration path must not contain NUL");
    }
}

} // namespace 

// Json をストリームから読み込み，オブジェクト内に重複した key が存在しないことを確認しながらパースする
// nlohmann::json::parse() には key の重複検出機能がないので，これをもたせる
nlohmann::json parse_json_document(std::istream& input)
{
    using Json = nlohmann::json;

    // key を記録する vector
    // "position": {"x": 1, "y": 2}, "rotation": {"x": 1, "y": 2}
    // のような "x" と "y" の重複は許したい
    std::vector<std::unordered_set<std::string>> object_keys;
    
    // key の重複を検査する関数
    const auto check_keys = 
    [&](
        int,                        // 入れ子構造の深さ
        Json::parse_event_t event,  // パース中に発生したイベント
        Json& parsed                // イベントに対応する JSON の値
    )
    {
        switch (event)
        {
        // { を読み込んだとき
        case Json::parse_event_t::object_start:
            object_keys.emplace_back(); // 末尾に新しい要素を追加
            break;

        // オブジェクトのキーを読み込んだとき
        // key の重複を検査する
        case Json::parse_event_t::key:
            // std::set::insert() の返り値は std::pair   
            // std::pair の 2 つ目の要素は bool 
            // 重複していたら false
            if (!object_keys.back().insert(parsed.get<std::string>()).second)
            {
                throw std::invalid_argument("Duplicate JSON member'" + parsed.get<std::string>() + "'");
            }
            break;

        // } を読み込んだとき
        case Json::parse_event_t::object_end:
            object_keys.pop_back(); // 末尾の要素を削除
            break;
        default:
            break;
        }
        return true;
    };

    auto result = Json::parse(input, check_keys);
    // ストリームに深刻なエラーが起きた場合
    if(input.bad())
        throw std::runtime_error("Failed to read JSON input");
    return result;
}

nlohmann::json load_json_document(const std::filesystem::path& path)
{
    require_path(path);
    std::ifstream input(path, std::ios::binary);

    // ストリームに異常がないかチェック
    if(!input)
    {
        throw std::runtime_error("Cannot open configuration: " + path_text(path));
    }

    // パースする
    try
    {
        return parse_json_document(input);
    }
    catch (const nlohmann::json::exception& error)
    {
        if(input.bad())
        {
            throw std::runtime_error("Failed to read configuration: " + path_text(path));
        }
        throw std::invalid_argument("Invalid JSON in '" + path_text(path) + "': " + error.what());
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument("Configuration '" + path_text(path) + "': " + error.what());
    }
}

void save_json_document(
    const std::filesystem::path& path,
    const nlohmann::json& document
)
{
    require_path(path);
    // nlohmann::json の内容を JSON 形式にシリアライズ
    // インデントを 2 スペースにする
    const std::string text = document.dump(4) + '\n';
    
    // 符号付整数型に収まるかどうか
    if(!std::in_range<std::streamsize>(text.size()))
    {
        throw std::length_error("Serialized configuration is too large");
    }

    // ファイルを開く
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if(!output)
    {
        throw std::runtime_error("Cannot open configuration for writing: " + path_text(path));
    }

    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output)
    {
        throw std::runtime_error("Failed to write configuration: " + path_text(path));
    }

}

} // namespace renderer::config_io