#version 330 core
in vec2 v_uv;
uniform sampler2D u_texture;
uniform vec2 u_resolution;
uniform float u_time,u_grain,u_bloom,u_aberration,u_scanlines,u_vignette,u_fade,u_gamma;
out vec4 out_color;
float noise(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233))+u_time*51.0)*43758.5453);}
void main(){
    vec2 pixel=1.0/u_resolution;vec2 offset=vec2(u_aberration,0)*pixel;
    vec3 c=texture(u_texture,v_uv).rgb;
    c.r=texture(u_texture,v_uv+offset).r;c.b=texture(u_texture,v_uv-offset).b;
    vec3 glow=vec3(0);
    for(int x=-2;x<=2;x++)for(int y=-2;y<=2;y++)glow+=max(texture(u_texture,v_uv+vec2(x,y)*pixel*3.0).rgb-vec3(.65),vec3(0))/25.0;
    c+=glow*u_bloom;
    c+=(noise(v_uv*u_resolution)-.5)*u_grain;
    c*=1.0-u_scanlines*(.5+.5*sin(v_uv.y*u_resolution.y*3.14159));
    vec2 center=v_uv*2.0-1.0;c*=max(0.0,1.0-u_vignette*dot(center,center)*.5);
    c=pow(max(c,vec3(0)),vec3(1.0/u_gamma))*(1.0-u_fade);
    out_color=vec4(c,1);
}
