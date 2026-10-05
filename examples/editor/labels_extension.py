"""The same reusable extension can be hosted by any SDK shell."""
API_VERSION = 1

def register(client):
    client.register('example.label', label)

def label(client, file, entity, value):
    document = client.document(file, 'scenes')
    data = document.data
    record = next(item for item in data.get('entities', []) if item.get('id') == entity)
    record.setdefault('extensions', {}).setdefault('example', {})['label'] = value
    document.replace(data).save()
    return document.data
