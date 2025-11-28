#!/bin/bash
# generate_https_certs.sh - 一键生成 HTTPS TLS 证书

set -e  # 遇到错误立即退出

# 配置参数
DOMAIN="${1:-localhost}"
DAYS="${2:-3650}"
KEY_SIZE="${3:-2048}"
CERT_FILE="${4:-server.cert}"
KEY_FILE="${5:-server.key}"

echo "正在为域名 '$DOMAIN' 生成 TLS 证书..."
echo "有效期: $DAYS 天"
echo "密钥长度: $KEY_SIZE 位"
echo "输出文件: $CERT_FILE, $KEY_FILE"
echo

# 生成私钥
echo "1. 生成私钥..."
openssl genrsa -out "$KEY_FILE" "$KEY_SIZE"

# 生成证书签名请求 (CSR) 和自签名证书
echo "2. 生成自签名证书..."
openssl req -new -x509 -key "$KEY_FILE" -out "$CERT_FILE" -days "$DAYS" \
  -subj "/C=CN/ST=Beijing/L=Beijing/O=MyCompany/CN=$DOMAIN" \
  -addext "subjectAltName = DNS:$DOMAIN,DNS:localhost,DNS:127.0.0.1,IP:127.0.0.1"

# 验证生成的文件
echo "3. 验证生成的证书..."
if [[ -f "$CERT_FILE" && -f "$KEY_FILE" ]]; then
    echo "✓ 证书生成成功！"
    echo
    echo "文件信息:"
    echo "  证书文件: $CERT_FILE"
    echo "  私钥文件: $KEY_FILE"
    echo
    echo "证书详情:"
    openssl x509 -in "$CERT_FILE" -text -noout | grep -E "Subject:|Not Before|Not After|DNS:"
else
    echo "✗ 证书生成失败！"
    exit 1
fi

echo
echo "使用方法:"
echo "  在服务器配置中指定:"
echo "    cert: $CERT_FILE"
echo "    key:  $KEY_FILE"
