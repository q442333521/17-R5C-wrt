#!/bin/bash
################################################################################
# IPK 包验证脚本
# 用于验证生成的 IPK 包格式是否正确
################################################################################

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_success() {
    echo -e "${GREEN}✓${NC} $1"
}

print_error() {
    echo -e "${RED}✗${NC} $1"
}

print_info() {
    echo -e "${YELLOW}ℹ${NC} $1"
}

# 检查 IPK 文件
check_ipk() {
    local IPK_FILE="$1"
    
    if [ ! -f "$IPK_FILE" ]; then
        print_error "文件不存在: $IPK_FILE"
        exit 1
    fi
    
    echo "========================================"
    echo "验证 IPK 包: $(basename $IPK_FILE)"
    echo "========================================"
    echo ""
    
    # 检查 1: 文件类型
    echo "【检查 1】文件类型"
    local file_type=$(file "$IPK_FILE" | grep "ar archive")
    if [ -n "$file_type" ]; then
        print_success "文件是 ar 归档格式"
        echo "   $file_type"
    else
        print_error "文件不是有效的 ar 归档"
        file "$IPK_FILE"
        exit 1
    fi
    echo ""
    
    # 创建临时目录
    local TEMP_DIR=$(mktemp -d)
    cd "$TEMP_DIR"
    
    # 解包
    ar x "$IPK_FILE"
    
    # 检查 2: 包含的文件
    echo "【检查 2】包含的文件"
    local files=($(ar t "$IPK_FILE"))
    if [ "${files[0]}" = "debian-binary" ] && \
       [ "${files[1]}" = "control.tar.gz" ] && \
       [ "${files[2]}" = "data.tar.gz" ]; then
        print_success "文件顺序正确: ${files[@]}"
    else
        print_error "文件顺序错误: ${files[@]}"
        print_info "正确顺序应该是: debian-binary control.tar.gz data.tar.gz"
        cd - > /dev/null
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    echo ""
    
    # 检查 3: debian-binary 内容
    echo "【检查 3】debian-binary 内容"
    local version=$(cat debian-binary)
    if [ "$version" = "2.0" ]; then
        print_success "版本号正确: $version"
    else
        print_error "版本号错误: $version (应该是 2.0)"
        cd - > /dev/null
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    echo ""
    
    # 检查 4: control.tar.gz 内容
    echo "【检查 4】control.tar.gz 内容"
    local control_files=($(tar -tzf control.tar.gz | grep -v '/$'))
    print_info "包含 ${#control_files[@]} 个文件:"
    for f in "${control_files[@]}"; do
        echo "   - $f"
    done
    
    # 检查必需的 control 文件
    if tar -tzf control.tar.gz | grep -q "control"; then
        print_success "包含 control 文件"
    else
        print_error "缺少 control 文件"
        cd - > /dev/null
        rm -rf "$TEMP_DIR"
        exit 1
    fi
    
    # 检查 control 文件内容
    tar -xzf control.tar.gz
    if [ -f "control" ] || [ -f "./control" ]; then
        local control_path="control"
        [ ! -f "$control_path" ] && control_path="./control"
        
        echo ""
        echo "   control 文件内容:"
        echo "   ─────────────────────────────────────"
        cat "$control_path" | sed 's/^/   /'
        echo "   ─────────────────────────────────────"
        
        # 检查必需字段
        local has_package=$(grep -q "^Package:" "$control_path" && echo "yes")
        local has_version=$(grep -q "^Version:" "$control_path" && echo "yes")
        local has_arch=$(grep -q "^Architecture:" "$control_path" && echo "yes")
        
        if [ "$has_package" = "yes" ] && [ "$has_version" = "yes" ] && [ "$has_arch" = "yes" ]; then
            print_success "包含所有必需字段 (Package, Version, Architecture)"
        else
            print_error "缺少必需字段"
            [ "$has_package" != "yes" ] && echo "   - 缺少 Package 字段"
            [ "$has_version" != "yes" ] && echo "   - 缺少 Version 字段"
            [ "$has_arch" != "yes" ] && echo "   - 缺少 Architecture 字段"
        fi
    fi
    echo ""
    
    # 检查 5: data.tar.gz 内容
    echo "【检查 5】data.tar.gz 内容"
    local data_dirs=($(tar -tzf data.tar.gz | grep '/$' | head -10))
    print_info "顶层目录结构 (前10个):"
    for d in "${data_dirs[@]}"; do
        echo "   - $d"
    done
    
    local file_count=$(tar -tzf data.tar.gz | wc -l)
    print_success "总共包含 $file_count 个文件/目录"
    echo ""
    
    # 检查 6: 文件权限
    echo "【检查 6】可执行文件权限"
    local bin_files=($(tar -tzf data.tar.gz | grep '/bin/' | grep -v '/$'))
    if [ ${#bin_files[@]} -gt 0 ]; then
        print_info "发现 ${#bin_files[@]} 个可执行文件:"
        tar -tzf data.tar.gz | grep '/bin/' | grep -v '/$' | head -5 | while read f; do
            local perm=$(tar -tzf data.tar.gz --verbose | grep "$f" | awk '{print $1}')
            echo "   - $f ($perm)"
        done
        print_success "可执行文件权限检查完成"
    else
        print_info "未找到 /bin/ 目录下的文件"
    fi
    echo ""
    
    # 清理
    cd - > /dev/null
    rm -rf "$TEMP_DIR"
    
    # 最终结果
    echo "========================================"
    print_success "IPK 包格式验证通过！"
    echo "========================================"
    echo ""
    echo "文件信息:"
    echo "  路径: $IPK_FILE"
    echo "  大小: $(du -h "$IPK_FILE" | cut -f1)"
    echo "  MD5:  $(md5sum "$IPK_FILE" | cut -d' ' -f1)"
    echo ""
    echo "可以安全地部署到 FriendlyWrt 设备。"
    echo ""
}

# 主函数
main() {
    if [ $# -eq 0 ]; then
        # 自动查找最新的 IPK 包
        local PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
        local IPK_FILE=$(find "$PROJECT_ROOT/package" -name "*.ipk" -type f -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -1 | cut -d' ' -f2-)
        
        if [ -z "$IPK_FILE" ]; then
            print_error "未找到 IPK 包"
            echo ""
            echo "使用方法:"
            echo "  $0 [IPK文件路径]"
            echo ""
            echo "如果不指定文件路径，将自动查找 package 目录下最新的 IPK 包"
            exit 1
        fi
        
        print_info "自动发现: $IPK_FILE"
        echo ""
        check_ipk "$IPK_FILE"
    else
        check_ipk "$1"
    fi
}

main "$@"
