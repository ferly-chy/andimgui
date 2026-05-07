#pragma once

#include <filesystem>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "ConfigStorage.h"

namespace Config {

// 字段单点声明在 ConfigFields.inl, X-macro 同步生成 Schema 与序列化注册表。
// 下游扩展: 在 include path 提供 ConfigFieldsExt.inl (X-macro 同语法) 即可并入 CFG。
struct Schema {
#define CONFIG_GROUP(label)
#define CONFIG_FIELD(type, name, def)            type name = def;
#define CONFIG_ARR(type, name, n, ...)           type name[n] = __VA_ARGS__;
#define CONFIG_NOSAVE(type, name, def)           type name = def;
#define CONFIG_NOSAVE_ARR(type, name, n, ...)    type name[n] = __VA_ARGS__;
#include "ConfigFields.inl"
#if __has_include("ConfigFieldsExt.inl")
#include "ConfigFieldsExt.inl"
#endif
#undef CONFIG_GROUP
#undef CONFIG_FIELD
#undef CONFIG_ARR
#undef CONFIG_NOSAVE
#undef CONFIG_NOSAVE_ARR
};

struct Field {
    std::string_view name;
    std::variant<bool*, int*, float*,
                 std::span<int>, std::span<float>> ref;
};

const std::vector<Field>& GetFields();

void Save(const std::filesystem::path& path);
void Load(const std::filesystem::path& path);

}  // namespace Config

extern Config::Schema CFG;
