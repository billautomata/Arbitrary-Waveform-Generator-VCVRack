/**
 * This file contains the entire implementation of the VCO3 demo module.
 *
 * The code for VCO3 is very, very close to the code for VCO2.
 *
 * We have removed may of the original comments from VCO1 and VCO2, and put
 * in new comments for significant changes.
 * 
 */

#include "demo-plugin.hpp"
#include <simd/vector.hpp>

using float_4 = simd::float_4;

/*
 * This time we are using our own sine approximation, rather than the
 * one from Fundamental VCO-1. This one is a little crude, not super
 * accurate, and really could be optimized more.
 * 
 * Here we do the calculations with two different 
 * equations depending on whether the radian angle is more or less than pi.
 * 
 * The equations themselves are very simple fourth order Taylor series, with
 * an arbitrary fudge factor added into to make it more accurate.
 * 
 * This can undoubtedly be improved.
 * 
 * input: _x must be >= 0, and <= 2 * pi.
 */

inline float_4 SquinkyLabs_sinTwoPi(float_4 _x) {
    const static float twoPi = 2 * 3.141592653589793238;
    const static float pi =  3.141592653589793238;
    _x -= ifelse((_x > float_4(pi)), float_4(twoPi), float_4::zero()); 

    float_4 xneg = _x < float_4::zero();
    float_4 xOffset = ifelse(xneg, float_4(pi / 2.f), float_4(-pi  / 2.f));
    xOffset += _x;
    float_4 xSquared = xOffset * xOffset;
    float_4 ret = xSquared * float_4(1.f / 24.f);
    float_4 correction = ret * xSquared *  float_4(.02 / .254);
    ret += float_4(-.5);
    ret *= xSquared;
    ret += float_4(1.f);

    ret -= correction;
    return ifelse(xneg, -ret, ret);    
}
 
// VCV has a limit of 16 channels in a polyphonic cable.
static const int maxPolyphony = 16;

// Since SIMD processes 4 floats at once,
// we will need four banks of four to make 16 voices.
static const int maxBanks = maxPolyphony / 4;

struct VCO4Module : Module
{
    enum ParamIds {
        PITCH_PARAM,
        NUM_PARAMS
	};
	enum InputIds {
        CV_INPUT,
        NUM_INPUTS
	};
	enum OutputIds {
        SAW_OUTPUT,
        SIN_OUTPUT,
        PARA_OUTPUT,
        NUM_OUTPUTS
	};
	enum LightIds {
        NUM_LIGHTS
    };

    // Notice that many of the variables that were "float"
    // before are now "float_4". float_4 is a vector of four
    // floats that fits in a single CPU register an can be processed
    // all at once.
    float_4 phaseAccumulators[maxBanks] = {0};
    float_4 phaseAdvance[maxBanks] = {0};
    dsp::MinBlepGenerator<16, 16, float_4> sawMinBlep[maxBanks];

    int currentPolyphony = 1;
    int currentBanks = 1;
    int loopCounter = 0;
    bool outputSaw = false;
    bool outputSin = false;
    bool outputPara = false;

    VCO4Module() {
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PITCH_PARAM, 0, 10, 4, "Initial Pitch");
    }

    void process(const ProcessArgs& args) override {
        if (loopCounter-- == 0) {
            loopCounter = 3;
            processEvery4Samples(args);
        }

        generateOutput();
    }

    void processEvery4Samples(const ProcessArgs& args) {
        currentPolyphony = std::max(1, inputs[CV_INPUT].getChannels());
        currentBanks = currentPolyphony / 4;
        if (currentPolyphony % 4) {
            ++currentBanks;
        }
        outputs[SIN_OUTPUT].setChannels(currentPolyphony);
        outputs[SAW_OUTPUT].setChannels(currentPolyphony);
        outputs[PARA_OUTPUT].setChannels(currentPolyphony);

        outputSaw = outputs[SAW_OUTPUT].isConnected();
        outputSin = outputs[SIN_OUTPUT].isConnected();
        outputPara = outputs[PARA_OUTPUT].isConnected();

        // Note that assigning a float to a float_4 silently copies the float into all
        // four floats in the float_4.
        float_4 pitchParam = params[PITCH_PARAM].value;
        for (int bank = 0; bank < currentBanks; ++bank) {
            const int currentChannel = bank * 4;

            // This API lets us fetch the CV for four channels at once.
            float_4 pitchCV = inputs[CV_INPUT].getPolyVoltageSimd<float_4>(currentChannel);
        
            // Normal arithmetic operators work transparently with float_4.
            // Sometimes when expressions mix float_4 and float you need
            // to explicitly convert, like we do here with the number f.
            // Other time we do it "just because".
            float_4 combinedPitch = pitchParam + pitchCV - float_4(4.f);

            const float_4 q = float(log2(261.626));       // move up to C
            combinedPitch += q;

            // Note that because rack's approxExp2_taylor5 is templatized, it works just
            // the same for a float_4 as it did for float.
            const float_4 freq = rack::dsp::approxExp2_taylor5<float_4>(combinedPitch);

            const float_4 normalizedFreq = float_4(args.sampleTime) * freq;
            phaseAdvance[bank] = normalizedFreq;
        }
    }

    void generateOutput() {
        for (int bank = 0; bank < currentBanks; ++bank) {
            const int baseChannel = bank * 4;
            const int relativeChannel = currentPolyphony - baseChannel;

            // Just as before, be advance the phase and wrap around 0.
            // This time we do four oscillators at once.
            phaseAccumulators[bank] += phaseAdvance[bank];
            phaseAccumulators[bank] -= simd::floor(phaseAccumulators[bank]);
           
            float_4 minBlepValue;
            if (outputSaw || outputPara) {

                // Evaluate the phase, and determine if we are at a discontinuity.
                // Determine if the saw "should have" already crossed .5V in the last sample period
                // This is the SIMD version of the minBlep code from VCO2. Well, more properly it's the
                // code from VCV Fundamental VCO-1. I find this algorithm pretty difficult to figure out
                // in SIMD. Thank goodness the Fundamental code is such a rich well to draw from.
                float_4 halfCrossing = (0.5f - (phaseAccumulators[bank] -  phaseAdvance[bank])) /  phaseAdvance[bank];
                int halfMask = simd::movemask((0 < halfCrossing) & (halfCrossing <= 1.f));
                if (halfMask) {
                    for (int subChannel=0; subChannel < relativeChannel; ++subChannel) {
                        if (halfMask & (1 << subChannel)) {
                            float_4 mask = simd::movemaskInverse<float_4>(1 << subChannel);
                            float jumpPhase = halfCrossing[subChannel] - 1.f;
                            float_4 jumpAmount = mask & -2.f;
                            sawMinBlep[bank].insertDiscontinuity(jumpPhase, jumpAmount);
                        }
                    }
                }

                minBlepValue = sawMinBlep[bank].process();
            }

            if (outputSaw) {
                float_4 rawSaw = phaseAccumulators[bank] + float_4(.5f);
                rawSaw -= simd::trunc(rawSaw);
                rawSaw = 2 * rawSaw - 1;

                rawSaw += minBlepValue;
                float_4 sawWave = float_4(5) * rawSaw;
                outputs[SAW_OUTPUT].setVoltageSimd(sawWave, baseChannel);
            }

            if (outputPara) {
                float_4 paraWave = phaseAccumulators[bank] + float_4(.5f);
                paraWave -= simd::trunc(paraWave);      // now 0 ... 1
                paraWave *= paraWave;                   // squared, but still 0..1
                paraWave = 2 * paraWave;                // now 0..2
                paraWave += minBlepValue;
                paraWave -= .33f * 2;                   // subtract out the DC component
                                                        // (use your calculus or trial and error).
                paraWave *= 5;
                outputs[PARA_OUTPUT].setVoltageSimd(paraWave, baseChannel);
            }

            if (outputSin) {
                const static float twoPi = 2 * 3.141592653589793238;
                float_4 sinWave = float_4(5.f) * SquinkyLabs_sinTwoPi( phaseAccumulators[bank] * twoPi);
                outputs[SIN_OUTPUT].setVoltageSimd(sinWave, baseChannel);
            }
        }
    }
};

struct VCO4Widget : ModuleWidget {
    VCO4Widget(VCO4Module* module) {
        setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/vco1_panel.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        float x = 50;
        float headingY = 20;
        float inputY = 75;
        float knobY = 130;
        float sawY = 190;
        float sinY = 240;
        float paraY = 290;
        float labelAbove = 20;

        addInput(createInput<PJ301MPort>(Vec(x, inputY), module, VCO4Module::CV_INPUT));
        addParam(createParam<RoundBlackKnob>(Vec(x-4, knobY), module, VCO4Module::PITCH_PARAM));
        addOutput(createOutput<PJ301MPort>(Vec(x, sawY), module, VCO4Module::SAW_OUTPUT));
        addOutput(createOutput<PJ301MPort>(Vec(x, sinY), module, VCO4Module::SIN_OUTPUT));
        addOutput(createOutput<PJ301MPort>(Vec(x, paraY), module, VCO4Module::PARA_OUTPUT));

        addLabel(Vec(20, headingY), "Demo VCO4");
        addLabel(Vec(x-16, inputY - labelAbove), "Pitch CV");
        addLabel(Vec(x-10, knobY - labelAbove), "Pitch");
        addLabel(Vec(x-16, sawY - labelAbove), "Saw Out");
        addLabel(Vec(x-16, sinY - labelAbove), "Sin Out");
        addLabel(Vec(x-16, paraY - labelAbove), "Para Out");
    }

    Label* addLabel(const Vec& v, const char* str)
    {
        NVGcolor black = nvgRGB(0,0,0);
        Label* label = new Label();
        label->box.pos = v;
        label->text = str;
        label->color = black;
        addChild(label);
        return label;
    }
};

Model* modelVCO4 = createModel<VCO4Module, VCO4Widget>("demo-vco4");