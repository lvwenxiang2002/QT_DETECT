import xml.etree.ElementTree as ET
ET.register_namespace('', 'http://schemas.microsoft.com/developer/msbuild/2003')
tree = ET.parse('HQ_HostApp.vcxproj')
root = tree.getroot()
ns = {'ns': 'http://schemas.microsoft.com/developer/msbuild/2003'}
for itemDefinitionGroup in root.findall('.//ns:ItemDefinitionGroup', ns):
    link = itemDefinitionGroup.find('ns:Link', ns)
    if link is not None:
        add_deps = link.find('ns:AdditionalDependencies', ns)
        if add_deps is not None and 'ControllerDll.lib' not in add_deps.text:
            add_deps.text = 'ControllerDll.lib;CommonToolDll.lib;' + add_deps.text
        add_lib_dirs = link.find('ns:AdditionalLibraryDirectories', ns)
        if add_lib_dirs is not None and 'Controller_SDK' not in add_lib_dirs.text:
            add_lib_dirs.text = r'D:\SoftWare\kangsida\Controller_SDK-V4.0.0.1(demo)\SDK\C++_DLL\x64;' + add_lib_dirs.text
tree.write('HQ_HostApp.vcxproj', encoding='utf-8', xml_declaration=True)

