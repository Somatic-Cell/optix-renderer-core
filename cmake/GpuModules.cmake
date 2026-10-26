# OptiX IR を CUDA ファイルから生成する
# target: 作成する CMake ターゲットの名前
# source: コンパイルする CUDA ソースコード
function(renderer_add_optix_module target source)
    # コンパイル結果そのものをファイルとして取得したいため，OBJECT を選択
    add_library(${target} OBJECT "${source}")
    set_target_properties(${target} PROPERTIES
        CUDA_OPTIX_COMPILATION ON           # OptiX IR にコンパイルする
        CUDA_RUNTIME_LIBRARY None           # CUDA Runtime library をリンクしない
        CUDA_SEPARABLE_COMPILATION OFF      # 個々のソースファイルを独立した OptiX のモジュールとして生成するので OFF (Callable のときは ON?)
        CUDA_STANDARD 20                    # C++ 20 を要求
        CUDA_STANDARD_REQUIRED ON           # C++ 20 を満たせない場合に古いバージョンへフォールバックすることを許さない
        CUDA_EXTENSIONS OFF                 # コンパイラ固有の拡張を要求しない
    )
    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/include" "${OPTIX_INCLUDE_DIR}")

    # NVCC に渡すオプション
    target_compile_options(
        ${target} PRIVATE 
        --generate-line-info    # デバイスコードの行番号を生成する
    )
    if(MSVC)
        target_compile_options(
            ${target} PRIVATE
            "-Xcompiler=/Zc:preprocessor,/utf-8,/Zc:__cplusplus" 
        )
    endif()
endfunction()

# 生成された OptiX IR を，レンダラが実行時に参照するディレクトリにコピーする
# application: 実行ファイルの CMake ターゲット名
# module: OptiX IR 生成ターゲット名
# method: レンダリング手法名
# filename: 配置先のファイル名
function(renderer_stage_optix_module application module method filename)
    set(_stage_target "${application}_${module}_stage")
    set(_module_directory "$<TARGET_FILE_DIR:${application}>/modules/${method}")
    
    add_custom_target(${_stage_target}
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_module_directory}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_OBJECTS:${module}>" "${_module_directory}/${filename}"
        COMMAND_EXPAND_LISTS
        VERBATIM
    )

    # ビルド順序の指定
    add_dependencies(${_stage_target} ${module})        # ステージング処理は OptiX IR 生成ターゲットに依存
    add_dependencies(${application} ${_stage_target})   # レンダラの実行ファイルターゲットはステージング処理に依存
endfunction()
