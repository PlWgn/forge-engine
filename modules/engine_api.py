"""Feature checks for reusable extensions; no dependency on a specific engine release."""
import forge

def require_api(version=1, *features):
    if type(version) is not int or version < 1:
        raise ValueError('API version must be a positive integer')
    info=forge.capabilities()
    if info['api_version'] < version:
        raise RuntimeError(f'Extension needs Forge API {version}; available: {info["api_version"]}')
    missing=set(features)-set(info['features'])
    if missing:
        raise RuntimeError('Missing Forge features: '+', '.join(sorted(missing)))
    return info
