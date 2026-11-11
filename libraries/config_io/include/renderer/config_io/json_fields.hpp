#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace renderer::config_io
{

// Json の値と C++ の値の間の型変換を行う
struct JsonValueCodec
{
    // JSON -> C++
    template<class T>
    void read(const nlohmann::json& document, T& value) const 
    {
        // 列挙型はこの構造体では扱わない
        static_assert(!std::is_enum_v<T>, "Use JsonEnumCodec for enum");
        // boolean かどうか調べる
        if constexpr (std::is_same_v<T, bool>)
        {
            if (!document.is_boolean())
            {
                throw std::invalid_argument("must be a boolean");
            }
            value = document.get<bool>();
        }
        // 整数型かどうか調べる
        else if constexpr (std::is_integral_v<T>)
        {
            // 浮動小数点を整数型として受け取らない
            if(document.is_number_unsigned())
            {
                const auto number = document.get<nlohmann::json::number_unsigned_t>();
                if(!std::in_range<T>(number))
                {
                    throw std::invalid_argument("integer is outside the destination type's range");
                }
                value = static_cast<T>(number);
            }
            else if(document.is_number_integer())
            {
                const auto number = document.get<nlohmann::json::number_integer_t>();
                if(std::in_range<T>(number))
                {
                    throw std::invalid_argument("integer is outside the destination type's range");
                }
                value = static_cast<T>(number);
            }
            else 
                throw std::invalid_argument("must be an integer");
        }
        // 浮動小数点小数かどうか調べる
        // 整数 -> 小数は許す
        else if constexpr (std::is_floating_point_v<T>)
        {
            if (!document.is_number())
            {
                throw std::invalid_argument("must be a number");
            }
            const double number = document.get<double>();

            if(
                !std::isfinite(number) 
                || number > (std::numeric_limits<T>::max)()
                || number < (std::numeric_limits<T>::lowest)()
            ){
                throw std::invalid_argument("must be a finite representative number");
            }
            value = static_cast<T>(number);
        }
        // これまでの条件に該当しない場合，nlohmann::json の変換を使う
        else 
        {
            document.get_to(value);
        }
    }

    // C++ -> JSON
    template<class T>
    [[nodiscard]] nlohmann::json write(const T& value) const 
    {
        static_assert(!std::is_enum_v<T>, "Use JsonEnumCodec for an enum");
        if constexpr (std::is_floating_point_v<T>)
        {
            // Json に NaN や Inf はないので，有限の値かどうかを確認
            if(!std::isfinite(value))
            {
                throw std::invalid_argument("cannot serialize a non-finite number");
            }
        }
        return nlohmann::json(value);
    }
};

// Enum: 列挙型, Count: 要素数
template<class Enum, std::size_t Count>
struct JsonEnumCodec
{
    static_assert(std::is_enum_v<Enum>);

    // 対応する表の参照
    const std::array<std::pair<std::string_view, Enum>, Count>& choices;    

    // JSON の文字列を C++ の列挙型に変換する
    void read(const nlohmann::json& document, Enum& value) const 
    {
        if(!document.is_string()){
            throw std::invalid_argument("must be a string");
        }
        const auto& name = document.get_ref<const std::string&>();

        // choices 内に一致する string_view がないかどうかをチェック
        // choices 内の strring_view を spelling に，Enum を candidate に
        for(const auto& [spelling, candidate] : choices)
        {
            // 対応する choices を見つけたら，値を代入して終了
            if(name == spelling)
            {
                value = candidate;
                return;
            }
        }
        throw std::invalid_argument("unsupported value '" + name + "'");
    }

    [[nodiscard]] nlohmann::json write(Enum value) const 
    {
        // 対応表を探す
        for(const auto& [spelling, candidate] : choices)
        {
            // value と一致する Enum を発見したら，文字列に変換
            if(value == candidate){
                return std::string(spelling);
            }
        }
        throw std::invalid_argument("cannot serialize an unsupported enum value");
    }
};

// テンプレート引数を明示せずに推論する
template<class Enum, std::size_t Count>
JsonEnumCodec(const std::array<std::pair<std::string_view, Enum>, Count>&)
    -> JsonEnumCodec<Enum, Count>;


class JsonObjectReader
{
public:
    explicit JsonObjectReader(const nlohmann::json& document) : document_(document)
    {
        if(!document.is_object())
        {
            throw std::invalid_argument("must be an object");
        }
    }

    // 任意のフィールドを読み込む
    template<class T, class Codec = JsonValueCodec>
    void  field(std::string_view name, T& value, Codec codec = {})
    {
        read_field(name, value, codec, false);
    }
    
    // 必須のフィールドを読み込む
    template<class T, class Codec = JsonValueCodec>
    void  required(std::string_view name, T& value, Codec codec = {})
    {
        read_field(name, value, codec, true);
    }

    // C++ 側で宣言されていない，未知のフィールドがないかどうかを検査する
    void finish() const
    {
        for(auto item = document_.begin(); item != document_.end(); ++item)
        {
            if(!declared_.contains(item.key()))
            {
                throw std::invalid_argument("unknown member '" + item.key() + "'");
            }
        }
    }

private:
    template<class T, class Codec>
    void read_field(std::string_view name, T& value, Codec codec, bool required)
    {
        const std::string key(name);
        if (!declared_.insert(key).second)
        {
            throw std::logic_error("Duplicate filed binding: " + key);
        }

        // JSON から，key に対応するフィールドを検索
        const auto item = document_.find(key);
        
        // 見つからなかった場合
        if(item == document_.end())
        {
            // 必須のフィールドなら例外を送出
            if(required)
            {
                throw std::invalid_argument(key + ": is required");
            }
            return;
        }

        // 見つけたフィールドに正しい型が入っているか確認し，value に格納
        try
        {
            codec.read(*item, value);
        }
        catch(const nlohmann::json::exception& error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
        catch(const std::invalid_argument& error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
    }

    const nlohmann::json& document_;
    std::set<std::string, std::less<>> declared_;   // これまでに field(), required() で宣言したフィールドを記録
};

// C++ の値を JSON オブジェクトに書き込む
class JsonObjectWriter
{
public:
    // 任意の field を
    template<class T, class Codec = JsonValueCodec>
    void field(std::string_view name, const T& value, Codec codec = {})
    {
        const std::string key(name);
        nlohmann::json encoded;
        try
        {
            encoded = codec.write(value);
        }
        catch (const nlohmann::json::exception& error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
        catch (const std::invalid_argument& error)
        {
            throw std::invalid_argument(key + ": " + error.what());
        }
        // 新しい要素を追加できなかった場合
        if (!document_.emplace(key, std::move(encoded)).second)
            throw std::logic_error("Duplicate field binding: " + key);
    }

    // 必須のフィールド
    // C++ 側に既に値が存在していることが確定しているため，field と同じ
    template<class T, class Codec = JsonValueCodec>
    void required(std::string_view name, const T& value, Codec codec = {})
    {
        field(name, value, codec);
    }

    // 完成した JSON オブジェクトの取得
    [[nodiscard]] nlohmann::json take() { return std::move(document_); }
private:
    nlohmann::json document_ = nlohmann::json::object();
};

// JSON の内容を C++ 側のオブジェクトに読み込む
template<class T, class Fields>
void read_json_fields(
    const nlohmann::json& document, 
    T& value,       
    Fields fields   // ラムダ式
)
{
    T candidate = value;
    JsonObjectReader reader(document);
    fields(reader, candidate);
    reader.finish();
    value = std::move(candidate);
}

// C++ から JSON を生成する
template<class T, class Fields>
[[nodiscard]] nlohmann::json write_json_fields(const T& value, Fields fields)
{
    JsonObjectWriter writer;
    fields(writer, value);
    return writer.take();
}

}