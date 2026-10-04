#version 330 core
in vec2 v_uv;
in vec4 v_vertex_color;
in vec3 v_normal, v_position;
uniform vec4 u_color;
uniform sampler2D u_texture, u_shadow_map;
uniform int u_textured, u_light_count, u_shadow_enabled;
uniform float u_lit;
uniform vec3 u_light_position[16], u_light_direction[16], u_light_color[16];
uniform vec4 u_light_params[16]; // type, intensity, range, cone cosine
uniform mat4 u_shadow_matrix;
uniform vec3 u_ambient;
uniform int u_pbr, u_alpha_mode, u_maps[6];
uniform sampler2D u_normal_map, u_mr_map, u_metallic_map, u_roughness_map, u_occlusion_map, u_emissive_map;
uniform float u_metallic, u_roughness, u_normal_scale, u_occlusion_strength, u_alpha_cutoff;
uniform vec3 u_emissive, u_camera_position;
out vec4 out_color;
vec3 srgbToLinear(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 linearToSrgb(vec3 c){c=max(c,vec3(0));return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(vec3(.0031308),c));}
vec3 surfaceNormal(){
    vec3 n=normalize(v_normal);
    if(u_maps[0]==0)return n;
    vec3 p1=dFdx(v_position),p2=dFdy(v_position);vec2 t1=dFdx(v_uv),t2=dFdy(v_uv);
    float det=t1.x*t2.y-t1.y*t2.x;
    if(abs(det)<1e-8)return n;
    vec3 t=(p1*t2.y-p2*t1.y)/det;t-=n*dot(n,t);
    if(length(t)<1e-8)return n;t=normalize(t);vec3 b=normalize(cross(n,t))*sign(det);
    vec3 sampleNormal=texture(u_normal_map,v_uv).xyz*2.-1.;sampleNormal.xy*=u_normal_scale;
    return normalize(mat3(t,b,n)*sampleNormal);
}
vec3 brdf(vec3 base,float metallic,float roughness,vec3 n,vec3 v,vec3 l){
    vec3 sum=v+l;vec3 h=length(sum)>1e-8?normalize(sum):n;
    float nv=max(dot(n,v),.0001),nl=max(dot(n,l),0.),nh=max(dot(n,h),0.),vh=max(dot(v,h),0.);
    float a=roughness*roughness,a2=a*a,d=nh*nh*(a2-1.)+1.;
    float distribution=a2/max(3.14159265*d*d,.000001);
    float k=(roughness+1.)*(roughness+1.)/8.;float geometry=(nv/(nv*(1.-k)+k))*(nl/(nl*(1.-k)+k));
    vec3 f0=mix(vec3(.04),base,metallic),f=f0+(1.-f0)*pow(1.-vh,5.);
    vec3 specular=distribution*geometry*f/max(4.*nv*max(nl,.0001),.0001);
    return ((1.-f)*(1.-metallic)*base/3.14159265+specular)*nl;
}

float visibility() {
    if(u_shadow_enabled==0) return 1.0;
    vec4 p=u_shadow_matrix*vec4(v_position,1);
    vec3 uv=p.xyz/p.w*.5+.5;
    if(any(lessThan(uv,vec3(0))) || any(greaterThan(uv,vec3(1))))return 1.0;
    float result=0.0;vec2 pixel=1.0/vec2(textureSize(u_shadow_map,0));
    for(int x=-1;x<=1;x++)for(int y=-1;y<=1;y++)
        result+=uv.z-.002<=texture(u_shadow_map,uv.xy+vec2(x,y)*pixel).r?1.0:.2;
    return result/9.0;
}
void main() {
    vec4 c=u_color*v_vertex_color;if(u_textured==1)c*=texture(u_texture,v_uv);
    if(u_alpha_mode==1 && c.a<u_alpha_cutoff)discard;
    if(u_alpha_mode==0)c.a=1.;else if(c.a<.01)discard;
    if(u_pbr==1 && u_lit>0.5){
        // Factors/vertex colors are linear; only color textures use sRGB encoding.
        vec3 base=u_color.rgb*v_vertex_color.rgb;
        if(u_textured==1)base*=srgbToLinear(texture(u_texture,v_uv).rgb);
        vec3 n=surfaceNormal(),offset=u_camera_position-v_position;
        vec3 v=length(offset)>1e-8?normalize(offset):n;float metallic=u_metallic,roughness=u_roughness;
        if(u_maps[1]==1){vec4 mr=texture(u_mr_map,v_uv);roughness*=mr.g;metallic*=mr.b;}
        if(u_maps[2]==1)metallic*=texture(u_metallic_map,v_uv).r;
        if(u_maps[3]==1)roughness*=texture(u_roughness_map,v_uv).r;
        metallic=clamp(metallic,0.,1.);roughness=clamp(roughness,.045,1.);
        float ao=u_maps[4]==1?mix(1.,texture(u_occlusion_map,v_uv).r,u_occlusion_strength):1.;
        vec3 result=u_ambient*base*(1.-metallic)*ao;
        if(u_light_count==0)result+=brdf(base,metallic,roughness,n,v,normalize(vec3(.5,1,.8)))*3.;
        for(int i=0;i<u_light_count;i++){
            vec3 delta=u_light_position[i]-v_position;float distance=length(delta);
            bool directional=u_light_params[i].x==1.;vec3 l=directional?normalize(-u_light_direction[i]):delta/max(distance,.00001);
            float attenuation=directional?1.:pow(max(0.,1.-distance/u_light_params[i].z),2.);
            if(u_light_params[i].x==2.)attenuation*=smoothstep(u_light_params[i].w,min(1.,u_light_params[i].w+.05),dot(-l,normalize(u_light_direction[i])));
            result+=brdf(base,metallic,roughness,n,v,l)*u_light_color[i]*u_light_params[i].y*attenuation*((directional && i==0)?visibility():1.);
        }
        vec3 emission=u_emissive;if(u_maps[5]==1)emission*=srgbToLinear(texture(u_emissive_map,v_uv).rgb);
        out_color=vec4(linearToSrgb(result+emission),c.a);return;
    }
    vec3 light=vec3(1);
    if(u_lit>0.5){
        vec3 normal=normalize(v_normal);
        if(u_light_count==0)light=vec3(.3+.7*max(dot(normal,normalize(vec3(.5,1,.8))),0));
        else {
            light=u_ambient;
            for(int i=0;i<u_light_count;i++){
                vec3 offset=u_light_position[i]-v_position;float distance=length(offset);
                bool directional=u_light_params[i].x==1.0;
                vec3 direction=directional?normalize(-u_light_direction[i]):normalize(offset);
                float attenuation=directional?1.0:pow(max(0.0,1.0-distance/u_light_params[i].z),2.0);
                if(u_light_params[i].x==2.0)attenuation*=smoothstep(u_light_params[i].w,min(1.0,u_light_params[i].w+.05),dot(-direction,normalize(u_light_direction[i])));
                float shadow=(directional && i==0)?visibility():1.0;
                light+=u_light_color[i]*u_light_params[i].y*max(dot(normal,direction),0)*attenuation*shadow;
            }
        }
    }
    out_color=vec4(c.rgb*light,c.a);
}
