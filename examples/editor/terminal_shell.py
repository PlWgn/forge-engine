"""Optional shell, using only the public SDK; no ImGui or import forge dependency.
Launch: python tools/forge.py shell --shell examples/editor/terminal_shell.py
"""
import json
API_VERSION = 1

def main(client):
    print('Forge terminal shell. Enter JSON requests; quit exits.')
    print('Example: {"op":"list","group":"scenes"}')
    while True:
        try:
            line = input('forge> ')
            if line.strip() in ('quit', 'exit'): return
            request = json.loads(line)
            op = request.pop('op')
            if op.startswith('extension.'):
                result = client.command(op[len('extension.'):], **request)
            else:
                result = client.request(op, **request)
            print(json.dumps(result, ensure_ascii=False, indent=2))
        except EOFError: return
        except Exception as error: print(str(error))
