import xml.etree.ElementTree as ET
import copy

ET.register_namespace('', 'http://schemas.microsoft.com/developer/msbuild/2003')
tree = ET.parse('HQ_HostApp.vcxproj')
root = tree.getroot()
ns = {'msbuild': 'http://schemas.microsoft.com/developer/msbuild/2003'}

# Add ClCompile for cpp
for itemGroup in root.findall('msbuild:ItemGroup', ns):
    for clCompile in itemGroup.findall('msbuild:ClCompile', ns):
        if clCompile.get('Include') == r'widgets\Dialogs.cpp':
            new_cl = ET.Element('ClCompile', {'Include': r'widgets\RoiSettingDialog.cpp'})
            itemGroup.append(new_cl)
            break

# Add ClCompile for moc
for itemGroup in root.findall('msbuild:ItemGroup', ns):
    for clCompile in itemGroup.findall('msbuild:ClCompile', ns):
        if clCompile.get('Include') == r'release\moc_Dialogs.cpp':
            new_cl = copy.deepcopy(clCompile)
            new_cl.set('Include', r'release\moc_RoiSettingDialog.cpp')
            itemGroup.append(new_cl)
            break

# Add CustomBuild for h
for itemGroup in root.findall('msbuild:ItemGroup', ns):
    for customBuild in itemGroup.findall('msbuild:CustomBuild', ns):
        if customBuild.get('Include') == r'widgets\Dialogs.h':
            new_cb = copy.deepcopy(customBuild)
            new_cb.set('Include', r'widgets\RoiSettingDialog.h')
            for child in new_cb:
                if child.text:
                    child.text = child.text.replace('Dialogs.h', 'RoiSettingDialog.h').replace('moc_Dialogs.cpp', 'moc_RoiSettingDialog.cpp')
            itemGroup.append(new_cb)
            break

tree.write('HQ_HostApp.vcxproj', encoding='utf-8', xml_declaration=True)
