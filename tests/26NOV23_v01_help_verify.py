"""📖 0.14.02 本地帮助、版本与相邻原生功能验收。

输入：doc/help.html、双语README、现有CMakeCache和只读GRIB测试夹具。
输出：logs/S4-Version-0.14.02/help-时间/ 下的构建/Qt日志、hash和result.json。
命令：C:\\Python312\\python.exe tests/26NOV23_v01_help_verify.py
无必需参数；顺序构建Release/Debug，检查链接与资源部署，不安装或发布。
"""
import datetime
import hashlib
from html.parser import HTMLParser
import json
import logging
import os
from pathlib import Path
import re
import subprocess


class HelpParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.links = []
        self.external_resources = []

    def handle_starttag(self, s_tag, l_attributes):
        d_attributes = dict(l_attributes)
        if 'id' in d_attributes:
            self.ids.append(d_attributes['id'])
        if s_tag == 'a':
            self.links.append(d_attributes.get('href', ''))
        if s_tag in ('script', 'img', 'iframe') or (s_tag == 'link' and d_attributes.get('rel') != 'canonical'):
            self.external_resources.append(d_attributes)


def digest(p_file):
    return hashlib.sha256(p_file.read_bytes()).hexdigest()


def main():
    p_root = Path(__file__).resolve().parents[1]
    p_log = p_root / 'logs/S4-Version-0.14.02' / ('help-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    p_log.mkdir(parents=True)
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s', handlers=[logging.StreamHandler(), logging.FileHandler(p_log / 'verify.log', encoding='utf-8')])
    d_env = os.environ.copy()
    d_env['QT_LOGGING_TO_CONSOLE'] = '1'
    d_report = {'status': 'RUNNING', 'sources': {}, 'documentation': {}, 'configurations': {}}
    l_inputs = [p_root / s_path for s_path in ['CMakeLists.txt', 'doc/help.html', 'README.md', 'README_CN.md', 'src/app/hdf_explorer.cpp', 'tests/drop_targets_test.cpp', 'tests/grib_browser_test.cpp', 'tools/package-windows.ps1']]
    l_inputs.extend(sorted((p_root / 'src/core').rglob('*.cpp')))
    d_report['sources'] = {str(p_file.relative_to(p_root)): digest(p_file) for p_file in l_inputs}

    def run(l_command, p_prefix, i_timeout=1200):
        logging.info('Start %s', p_prefix)
        with p_prefix.with_suffix('.stdout.txt').open('wb') as o_stdout, p_prefix.with_suffix('.stderr.txt').open('wb') as o_stderr:
            o_result = subprocess.run(list(map(str, l_command)), cwd=p_root, env=d_env, stdout=o_stdout, stderr=o_stderr, timeout=i_timeout)
        assert o_result.returncode == 0, (str(p_prefix), o_result.returncode)
        logging.info('PASS %s', p_prefix)

    try:
        s_html = (p_root / 'doc/help.html').read_text(encoding='utf-8')
        o_parser = HelpParser()
        o_parser.feed(s_html)
        assert len(set(o_parser.ids)) == len(o_parser.ids), 'Duplicate HTML id'
        assert all(s_link.startswith('#') and s_link[1:] in o_parser.ids for s_link in o_parser.links), 'Broken help anchor'
        assert not o_parser.external_resources, 'Offline help should be self contained'
        assert all(s_id in o_parser.ids for s_id in ['zh-ai', 'en-ai', 'zh-grib', 'en-grib'])
        d_report['documentation']['help_anchors'] = len(o_parser.links)
        for s_name in ['README.md', 'README_CN.md']:
            s_text = (p_root / s_name).read_text(encoding='utf-8')
            assert len(s_text.splitlines()) <= 100, 'README should stay concise'
            assert '## ⬇️ Download' in s_text and 'Version 0.14.02' in s_text if s_name == 'README.md' else '## ⬇️ Download' in s_text and '版本 0.14.02' in s_text
            assert '**[Download](https://github.com/DuLiangMin/HDFStudio/releases/download/' in s_text
            assert 'win64.zip)' in s_text and 'LLM' in s_text and 'GRIB1' in s_text
            for s_target in re.findall(r'\]\(([^)]+)\)', s_text):
                if s_target.startswith(('http://', 'https://', '#')):
                    continue
                s_file, s_separator, s_anchor = s_target.partition('#')
                assert (p_root / s_file).exists(), (s_name, s_target)
                if s_separator and s_file == 'doc/help.html':
                    assert s_anchor in o_parser.ids, s_target
            d_report['documentation'][s_name] = {'lines': len(s_text.splitlines()), 'links': 'PASS'}

        s_cache = (p_root / 'build/vs2022/CMakeCache.txt').read_text(encoding='utf-8-sig')
        s_cmake = next(s_line.split('=', 1)[1] for s_line in s_cache.splitlines() if s_line.startswith('CMAKE_COMMAND:INTERNAL='))
        run([s_cmake, '-S', '.', '-B', 'build/vs2022'], p_log / 'configure')
        l_targets = ['hdf_explorer', 'hdf_explorer_drop_tests', 'grib_browser_tests', 'hovmoller_tests', 'grib_integration_tests', 'grib_gui_tests', 'hdf_filters_tests']
        l_browser_cases = re.findall(r'  void (\w+)\(\)', (p_root / 'tests/grib_browser_test.cpp').read_text(encoding='utf-8'))
        l_browser_cases = [s_case for s_case in l_browser_cases if 'External' not in s_case]
        l_main_cases = ['localHelpOpensBesideExecutableRegardlessOfWorkingDirectory', 'startupSplashIsPlainVisibleWidgetNotQSplashScreen', 'acceptsExistingLocalFile', 'rejectsMissingLocalFile', 'classicNetcdfOpensWithTreeGridImagePropertiesAndExport', 'cf18DiagnosticsReportScopedReferencesAndMissingDeclaration']
        for s_config in ['Release', 'Debug']:
            p_config = p_log / s_config
            p_config.mkdir()
            p_bin = p_root / 'build/output' / s_config
            run([s_cmake, '--build', 'build/vs2022', '--config', s_config, '--target', *l_targets, '--parallel', '6'], p_config / 'build')
            assert digest(p_bin / 'doc/help.html') == digest(p_root / 'doc/help.html')
            run([p_bin / 'HDFStudio.exe', '--version'], p_config / 'version', 30)
            assert (p_config / 'version.stdout.txt').read_text(encoding='utf-8-sig').strip() == 'HDFStudio 0.14.02'
            d_config = {'version': '0.14.02', 'help_deployment': 'PASS', 'exe_sha256': digest(p_bin / 'HDFStudio.exe'), 'tests': {}}
            l_suites = [('main', 'hdf_explorer_drop_tests', l_main_cases), ('browser', 'grib_browser_tests', l_browser_cases), ('section', 'hovmoller_tests', []), ('core', 'grib_integration_tests', []), ('scheduler', 'grib_gui_tests', []), ('filters', 'hdf_filters_tests', [])]
            for s_label, s_binary, l_cases in l_suites:
                p_test = p_config / (s_label + '.txt')
                run([p_bin / (s_binary + '.exe'), *l_cases, '-o', str(p_test) + ',txt'], p_config / s_label, 300)
                s_output = p_test.read_text(encoding='utf-8-sig')
                o_match = re.search(r'Totals: (\d+) passed, 0 failed, 0 skipped', s_output)
                assert o_match, s_output
                d_config['tests'][s_label] = int(o_match.group(1))
            s_ctest = str(Path(s_cmake).with_name('ctest.exe'))
            run([s_ctest, '--test-dir', 'build/vs2022', '-C', s_config, '-R', '^(hovmoller_tests|grib_integration_tests|grib_gui_tests|hdf_filters_tests)$', '--output-on-failure'], p_config / 'ctest', 300)
            d_config['ctest'] = '4/4'
            d_report['configurations'][s_config] = d_config
        for s_file, s_hash in d_report['sources'].items():
            assert digest(p_root / s_file) == s_hash, s_file
        d_report['status'] = 'PASS'
    except Exception:
        d_report['status'] = 'FAIL'
        logging.exception('Help/version verification failed')
        raise
    finally:
        (p_log / 'result.json').write_text(json.dumps(d_report, ensure_ascii=False, indent=2), encoding='utf-8')
    logging.info('PASS %s', p_log)


if __name__ == '__main__':
    main()
