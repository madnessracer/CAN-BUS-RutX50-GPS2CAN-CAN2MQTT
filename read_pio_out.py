from pathlib import Path
path = Path('pio_out.txt')
text = path.read_text('utf-16')
lines = text.splitlines()
print(len(lines))
for i, line in enumerate(lines[-80:], start=len(lines)-79):
    print(f'{i}: {line}')
