#include "ofMain.h"

// References.
// An investigation of fast real-time GPU-based image blur algorithms
// https://software.intel.com/en-us/blogs/2014/07/15/an-investigation-of-fast-real-time-gpu-based-image-blur-algorithms
//
// Approximating a Gaussian using a box filter
// http://nghiaho.com/?p=1159

class ofxIterativeBoxBlur
{
public:
	
	ofxIterativeBoxBlur()
		: inited(false)
		, radius(0)
		, num_iteration(3)
		, downsample_scale(0.5)
	{}
	
	void process(ofFbo& inout) { process(inout.getTexture(), inout); }
	
	void process(ofTexture& in, ofFbo& out)
	{
		if (pingpong.size())
		{
			if (pingpong[0].front->getWidth() != in.getWidth() * pingpong[0].scale
                || pingpong[0].front->getHeight() != in.getHeight() * pingpong[0].scale) {
                reset();
            }
		}
		
		ofPushStyle();
		
		if (inited == false)
		{
			inited = true;
			init(in.getWidth(), in.getHeight());
		}

		if (radius > 0)
		{
			float max_blur_radius = 14;
			
			int index = 0;
			for (int i = 0; i < NUM_DOWNSAMPLE_BUFFERS; i++)
			{
				float r = max_blur_radius / pingpong[i].scale;
				if (r < radius) index = i;
			}
			
			float s = radius * pingpong[index].scale * 0.5;

			blur_shader.begin();
			blur_shader.setUniform1f("blur_size", s);
			blur_shader.end();

			ofTexture& tex = renderPass(index, in);

			// write to out
			out.begin();
			{
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_SRC_ALPHA,GL_ONE);
                ofClear(255);
				tex.draw(0, 0, out.getWidth(), out.getHeight());
			}
			out.end();
		}
		else
		{
			out.begin();
			{
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_SRC_ALPHA,GL_ONE);
                ofClear(255);
				in.draw(0, 0, out.getWidth(), out.getHeight());
			}
			out.end();
		}

		ofPopStyle();
	}
	
	void setRadius(float v) { radius = v; }
	float getRadius() const { return radius; }
	
	void setNumIteration(int v) { num_iteration = v; }
	
	void setDownsampleScale(float v)
	{
		if (v <= 0) return;
		if (downsample_scale == v) return;
		
		downsample_scale = v;
		reset();
	}
	
	void reset()
	{
		inited = false;
		blur_shader.unload();
		pingpong.clear();
	}
	
protected:
	
	bool inited;
	float radius;
	int num_iteration;
	float downsample_scale;

	ofShader blur_shader;
	
	enum {
		NUM_DOWNSAMPLE_BUFFERS = 4
	};
	
	struct PingPong {
        ofFbo fbo[2];
		ofFbo *front, *back;
		
		ofVboMesh mesh;
		float scale;
		
		void allocate(float w, float h, float scale) {
			this->scale = scale;

			ofFbo::Settings s;
			s.width = w * scale;
			s.height = h * scale;
			s.internalformat = GL_RGBA;
			s.minFilter = GL_LINEAR;
			s.maxFilter = GL_LINEAR;
			s.textureTarget = GL_TEXTURE_2D;
			fbo[0].allocate(s);
			fbo[1].allocate(s);
			
			ofVec3f TC = fbo[0].getTexture().getCoordFromPercent(1, 1);
			
			mesh.setMode(OF_PRIMITIVE_TRIANGLE_STRIP);
			mesh.addTexCoord(ofVec2f(0, 0));
            mesh.addVertex(glm::vec3(0, 0, 0));
			mesh.addTexCoord(ofVec2f(TC.x, 0));
			mesh.addVertex(glm::vec3(s.width, 0, 0));
			mesh.addTexCoord(ofVec2f(0, TC.y));
			mesh.addVertex(glm::vec3(0, s.height, 0));
			mesh.addTexCoord(ofVec2f(TC.x, TC.y));
			mesh.addVertex(glm::vec3(s.width, s.height, 0));
			
			front = &fbo[0];
			back = &fbo[1];
		}
		
		void swap() { std::swap(front, back); }
	};
	
	vector<PingPong> pingpong;

	void init(float w, float h)
	{
		loadShader();

		pingpong.resize(NUM_DOWNSAMPLE_BUFFERS);
		
		float r = downsample_scale;

		for (int i = 0; i < NUM_DOWNSAMPLE_BUFFERS; i++)
		{
			pingpong[i].allocate(w, h, r);
			r *= 0.5;
		}
	}
	
	void loadShader()
	{
#define GLSL(CODE) "#version 120\n" \
	"#ifdef GL_ES\nprecision mediump float;\n#endif\n" \
	#CODE
		
		blur_shader.setupShaderFromSource(GL_FRAGMENT_SHADER, GLSL(
			uniform sampler2D tex;
			uniform vec2 direction;
			uniform float blur_size;

			void main() {
				vec2 TC = gl_TexCoord[0].xy;
				
				const int N = 16;
				float delta = blur_size / float(N);

				vec4 color = texture2D(tex, TC).rgba;

				for (int i = 0; i < N; i++) {
					vec2 d = direction * (float(i) * delta);
					color += texture2D(tex, TC + d).rgba;
					color += texture2D(tex, TC - d).rgba;
				}
				color /= float(N) * 2 + 1;

                gl_FragColor = color; //vec4(color, 1.0);
			}
		));
		blur_shader.linkProgram();

#undef GLSL
	}
	
	ofTexture& renderPass(int index, ofTexture& in)
	{
        PingPong &p = pingpong[index];
		// write to front
		p.front->begin();
        {
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_SRC_ALPHA,GL_ONE);
            ofClear(255);
            ofSetColor(255);
            in.draw(0, 0,
                    p.front->getWidth(),
                    p.front->getHeight());
        }
        p.front->end();
		
		for (int i = 0; i < num_iteration; i++)
		{
			// horizontal pass
			p.back->begin();
			{
				blur_shader.begin();
				blur_shader.setUniformTexture("tex", p.front->getTexture(), 1);
				blur_shader.setUniform2f("direction", 1. / p.front->getWidth(), 0);
				p.mesh.draw();
				blur_shader.end();
			}
			p.back->end();
			
			p.swap();
			
			// vertical pass
			p.back->begin();
			{
				blur_shader.begin();
				blur_shader.setUniformTexture("tex", p.front->getTexture(), 1);
				blur_shader.setUniform2f("direction", 0, 1. / p.front->getWidth());
				p.mesh.draw();
				blur_shader.end();
			}
			p.back->end();
			
			p.swap();
		}
		
		return p.front->getTexture();
	}
};
