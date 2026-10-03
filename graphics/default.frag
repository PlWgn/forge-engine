#version 330 core
in vec2 v_uv;
in vec3 v_normal, v_position;
uniform vec4 u_color;
uniform sampler2D u_texture, u_shadow_map;
uniform int u_textured, u_light_count, u_shadow_enabled;
uniform float u_lit;
uniform vec3 u_light_position[16], u_light_direction[16], u_light_color[16];
uniform vec4 u_light_params[16]; // type, intensity, range, cone cosine
uniform mat4 u_shadow_matrix;
uniform vec3 u_ambient;
out vec4 out_color;
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
    vec4 c=u_color;if(u_textured==1)c*=texture(u_texture,v_uv);
    if(c.a<.01)discard;
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
