import sys

def rw(path, content=None):
    encodings = ['utf-8-sig', 'utf-8', 'gbk']
    if content is None:
        with open(path, 'rb') as f:
            b = f.read()
        for enc in encodings:
            try:
                return b.decode(enc), enc
            except:
                pass
        raise Exception('Decode failed')
    else:
        with open(path, 'wb') as f:
            f.write(content[0].encode(content[1]))

try:
    mw_text, mw_enc = rw('E:\\BLL_COLD\\BLL_cold\\MainWindow.cpp')
    raw_img, _ = rw('C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\d478ca55-33c5-42d6-937a-5461f4f3a4ec\\scratch\\onRawImageCaptured.cpp')
    img_proc, _ = rw('C:\\Users\\Administrator\\.gemini\\antigravity\\brain\\d478ca55-33c5-42d6-937a-5461f4f3a4ec\\scratch\\onImageProcessed.cpp')

    lines = mw_text.splitlines()
    start_idx = -1
    for i, line in enumerate(lines):
        if 'void MainWindow::onImageProcessed' in line:
            start_idx = i
            break

    end_idx = -1
    for i in range(start_idx, len(lines)):
        if 'void MainWindow::onMenuConfigClicked' in lines[i]:
            end_idx = i
            break

    new_content = '\n'.join(lines[:start_idx]) + '\n' + raw_img + '\n\n' + img_proc + '\n\n' + '\n'.join(lines[end_idx:])
    rw('E:\\BLL_COLD\\BLL_cold\\MainWindow.cpp', (new_content, mw_enc))
    print('SUCCESS')
except Exception as e:
    print('ERROR:', e)
