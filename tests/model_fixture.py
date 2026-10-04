"""Original minimal animated skinned glTF fixture; no external test assets required."""
import base64, json, struct

def animated_triangle(path):
    data=bytearray();views=[];accessors=[]
    def add(values, code, component, kind, count, **extra):
        while len(data)%4:data.append(0)
        raw=struct.pack('<'+code*len(values),*values);offset=len(data);data.extend(raw)
        views.append(dict(buffer=0,byteOffset=offset,byteLength=len(raw)))
        accessors.append(dict(bufferView=len(views)-1,componentType=component,count=count,type=kind,**extra))
        return len(accessors)-1
    positions=add([-.8,-.6,0,.8,-.6,0,0,.8,0],'f',5126,'VEC3',3,min=[-.8,-.6,0],max=[.8,.8,0])
    joints=add([0]*12,'H',5123,'VEC4',3)
    weights=add([1,0,0,0]*3,'f',5126,'VEC4',3)
    inverse=add([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'f',5126,'MAT4',1)
    times=add([0,1],'f',5126,'SCALAR',2,min=[0],max=[1])
    translations=add([0,0,0,1,0,0],'f',5126,'VEC3',2)
    model=dict(asset={'version':'2.0','generator':'Forge regression fixture'},
        buffers=[dict(byteLength=len(data),uri='data:application/octet-stream;base64,'+base64.b64encode(data).decode())],
        bufferViews=views,accessors=accessors,
        materials=[dict(pbrMetallicRoughness=dict(baseColorFactor=[.3,.8,.4,1],metallicFactor=0))],
        meshes=[dict(primitives=[dict(attributes={'POSITION':positions,'JOINTS_0':joints,'WEIGHTS_0':weights},material=0)])],
        nodes=[dict(name='Root',children=[1,2]),dict(name='Triangle',mesh=0,skin=0),dict(name='Joint')],
        skins=[dict(joints=[2],inverseBindMatrices=inverse,skeleton=2)],
        animations=[dict(name='Move',samplers=[dict(input=times,output=translations,interpolation='LINEAR')],
                         channels=[dict(sampler=0,target={'node':2,'path':'translation'})])],scenes=[dict(nodes=[0])],scene=0)
    path.write_text(json.dumps(model),encoding='utf-8')

def authoring_triangle(path, *, joint='Joint', rest=0):
    """Original skinned mesh with Move/Idle/Raise clips and a named morph target."""
    animated_triangle(path)
    model=json.loads(path.read_text())
    data=bytearray(base64.b64decode(model['buffers'][0]['uri'].split(',')[1]))
    def add(values,kind,count,**extra):
        while len(data)%4:data.append(0)
        raw=struct.pack('<'+'f'*len(values),*values)
        model['bufferViews'].append(dict(buffer=0,byteOffset=len(data),byteLength=len(raw)));data.extend(raw)
        model['accessors'].append(dict(bufferView=len(model['bufferViews'])-1,componentType=5126,count=count,type=kind,**extra))
        return len(model['accessors'])-1
    model['nodes'][2]['name']=joint
    model['nodes'][2]['translation']=[rest,0,0]
    # Bind pose follows the renamed/proportioned target skeleton.
    inverse=add([1,0,0,0,0,1,0,0,0,0,1,0,-rest,0,0,1],'MAT4',1)
    model['skins'][0]['inverseBindMatrices']=inverse
    idle=add([rest,0,0,rest,0,0],'VEC3',2)
    raise_=add([rest,0,0,rest,1,0],'VEC3',2)
    times=model['animations'][0]['samplers'][0]['input']
    for name,values in [('Idle',idle),('Raise',raise_)]:
        model['animations'].append(dict(name=name,samplers=[dict(input=times,output=values,interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=2,path='translation'))]))
    delta=add([0,0,0,0,0,0,0,1,0],'VEC3',3,min=[0,0,0],max=[0,1,0])
    model['meshes'][0]['primitives'][0]['targets']=[dict(POSITION=delta)]
    model['meshes'][0]['extras']={'targetNames':['Smile']}
    model['meshes'][0]['weights']=[0]
    morph=add([0,1],'SCALAR',2)
    model['animations'][0]['samplers'].append(dict(input=times,output=morph,interpolation='LINEAR'))
    model['animations'][0]['channels'].append(dict(sampler=1,target=dict(node=1,path='weights')))
    model['buffers']=[dict(byteLength=len(data),uri='data:application/octet-stream;base64,'+base64.b64encode(data).decode())]
    path.write_text(json.dumps(model),encoding='utf-8')
