add_rules("mode.debug", "mode.release")

add_repositories("levimc-repo https://github.com/LiteLDev/xmake-repo.git")
-- add_requires("levilamina x.x.x") for a specific version
-- add_requires("levilamina develop") to use develop version
-- please note that you should add bdslibrary yourself if using dev version
if is_config("target_type", "server") then
    add_requires("levilamina 26.32.2", {configs = {target_type = "server"}})
else
    add_requires("levilamina 26.20.0", {configs = {target_type = "client"}})
end

add_requires("levibuildscript")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

option("target_type")
    set_default("server")
    set_showmenu(true)
    set_values("server", "client")
option_end()

target("Catalyst") 
    set_toolset("cxx", "C:/Program Files/LLVM/bin/clang-cl.exe")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    add_cxflags( "/EHa", "/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
    add_defines("NOMINMAX", "UNICODE", "CATALYST_EXPORTS")
    add_packages("levilamina")
    set_exceptions("none") -- To avoid conflicts with /EHa.
    set_kind("shared")
    set_languages("c++20")
    set_optimize("fast")
    add_headerfiles("src/(catalyst/**.h)")
    add_files("src/**.cpp")
    add_includedirs("src")
    set_symbols("debug")
    after_build(function (target)
        local lang_src = path.join(os.projectdir(), "lang")
        local lang_dst = path.join(os.projectdir(), "bin", target:name())
        if os.isdir(lang_src) then
            os.cp(lang_src, lang_dst)
            print("Copied lang folder to " .. lang_dst)
        end
    end)
    if is_config("target_type", "server") then
        add_defines("LL_PLAT_S")
    --  add_includedirs("src-server")
    --  add_files("src-server/**.cpp")
    else
        add_defines("LL_PLAT_C")
    --  add_includedirs("src-client")
    --  add_files("src-client/**.cpp")
    end
