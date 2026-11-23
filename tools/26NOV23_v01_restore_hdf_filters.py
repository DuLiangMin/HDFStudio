"""🧩 恢复公开源码省略的固定版本HDF解码依赖，逐文件验证SHA256。

输入：third_party/hdf_filters/sources.json；可选--root指定源树根目录。
输出：该根目录third_party/hdf_filters下的原始文件，不覆盖不同内容。
命令：python tools/26NOV23_v01_restore_hdf_filters.py [--root PATH]
网络使用环境HTTP_PROXY/HTTPS_PROXY；无需安装Python扩展包，不执行下载代码。
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import urllib.request


def main():
    o_parser = argparse.ArgumentParser(description=__doc__)
    o_parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    o_args = o_parser.parse_args()
    p_directory = o_args.root.resolve() / 'third_party/hdf_filters'
    d_manifest = json.loads((p_directory / 'sources.json').read_text(encoding='utf-8'))

    def restore(d_entry):
        p_file = (p_directory / d_entry['file']).resolve()
        if not p_file.is_relative_to(p_directory.resolve()):
            raise ValueError('Invalid manifest path')
        if p_file.exists():
            if hashlib.sha256(p_file.read_bytes()).hexdigest() != d_entry['sha256']:
                raise ValueError('Existing file differs: ' + str(p_file))
            return 'verified'
        if not d_entry['url'].startswith('https://raw.githubusercontent.com/'):
            raise ValueError('Unexpected source host')
        with urllib.request.urlopen(d_entry['url'], timeout=60) as o_response:
            b_data = o_response.read()
        if hashlib.sha256(b_data).hexdigest() != d_entry['sha256']:
            raise ValueError('Downloaded file hash differs: ' + d_entry['file'])
        p_file.parent.mkdir(parents=True, exist_ok=True)
        with p_file.open('xb') as o_output:
            o_output.write(b_data)
        return 'restored'

    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as o_pool:
        l_results = list(o_pool.map(restore, d_manifest['files']))
    print('Verified:', len(l_results), 'Restored:', l_results.count('restored'))


if __name__ == '__main__':
    main()
