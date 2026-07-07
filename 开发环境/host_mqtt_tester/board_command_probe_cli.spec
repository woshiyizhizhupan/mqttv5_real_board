# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['D:\\code\\唐家湾嵌入式兼职\\开发环境\\host_mqtt_tester\\packaging_entrypoints\\board_command_probe_cli.py'],
    pathex=['D:\\code\\唐家湾嵌入式兼职\\开发环境\\host_mqtt_tester'],
    binaries=[],
    datas=[],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='board_command_probe_cli',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='board_command_probe_cli',
)
