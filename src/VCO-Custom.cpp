/**
 * This file contains the entire implementation of the VCOCustom demo module.
 * Although it is possible to implement more than one module in a single file,
 * it is rarely done.
 */

#include "demo-plugin.hpp"
 
// VCV has a limit of 16 channels in a polyphonic cable.
static const int maxPolyphony = engine::PORT_MAX_CHANNELS;

/**
 *  Every synth module must have a Module structure.
 *  This is where all the real-time processing code goes.
 */
struct VCOCustomModule : Module
{
    enum ParamIds {
        PITCH_PARAM,
        WAVE0_PARAM,
        WAVE1_PARAM,
        WAVE2_PARAM,
        WAVE3_PARAM,
        WAVE4_PARAM,
        WAVE5_PARAM,
        WAVE6_PARAM,
        WAVE7_PARAM,
        WAVE8_PARAM,
        WAVE9_PARAM,
        WAVE10_PARAM,
        WAVE11_PARAM,
        WAVE12_PARAM,
        WAVE13_PARAM,
        WAVE14_PARAM,
        WAVE15_PARAM,
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

    float phaseAccumulators[maxPolyphony] = {};
    float phaseAdvance[maxPolyphony] = {};
    int currentPolyphony = 1;
    int loopCounter = 0;
    bool outputSaw = false;
    bool outputSin = false;
    bool outputPara = false;

    int indexToSampleFrom = 0;

    VCOCustomModule() {
        // Your module must call config from its constructor, passing in
        // how many ins, outs, etc... it has.
        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(PITCH_PARAM, 0, 10, 0, "Initial Pitch");
        configParam(WAVE0_PARAM, -5, 5, 0, "Wave 0");
        configParam(WAVE1_PARAM, -5, 5, 0, "Wave 1");
        configParam(WAVE2_PARAM, -5, 5, 0, "Wave 2");
        configParam(WAVE3_PARAM, -5, 5, 0, "Wave 3");
        configParam(WAVE4_PARAM, -5, 5, 0, "Wave 4");
        configParam(WAVE5_PARAM, -5, 5, 0, "Wave 5");
        configParam(WAVE6_PARAM, -5, 5, 0, "Wave 6");
        configParam(WAVE7_PARAM, -5, 5, 0, "Wave 7");
        configParam(WAVE8_PARAM, -5, 5, 0, "Wave 8");
        configParam(WAVE9_PARAM, -5, 5, 0, "Wave 9");
        configParam(WAVE10_PARAM, -5, 5, 0, "Wave 10");
        configParam(WAVE11_PARAM, -5, 5, 0, "Wave 11");
        configParam(WAVE12_PARAM, -5, 5, 0, "Wave 12");
        configParam(WAVE13_PARAM, -5, 5, 0, "Wave 13");
        configParam(WAVE14_PARAM, -5, 5, 0, "Wave 14");                
        configParam(WAVE15_PARAM, -5, 5, 0, "Wave 15");                
    }

    // Every Module has a process function. This is called once every
    // sample, and must service all the inputs and outputs of the module.
    void process(const ProcessArgs& args) override {

        // There are usually some thing that don't need to be done every single sample.
        // For example: looking at a knob position. You can some CPU if you do 
        // this less often.
        // Note that doing it like we do here will mean that running audio rate signals into the
        // V/Octave input will sound different and arguably not "correct". We think that's a reasonable
        // trade-off, but if you do this optimization make sure a) that you are aware of the trade-offs
        // in your plugin, and b) that it really is speeding up your code.
        if (loopCounter-- == 0) {
            loopCounter = 4;
            processEvery4Samples(args);
        }

        generateOutput();
    }

    void processEvery4Samples(const ProcessArgs& args) {
        // Is is very important that you tell the output ports how
        // many channels the should export. In a VCO it is very common
        // to use the number of CV input channels to automatically determine the
        // polyphony.
        // It is also very common to always run one channel, even if there is
        // no input. This lets the VCO generate output with no input.
        currentPolyphony = std::max(1, inputs[CV_INPUT].getChannels());
        outputs[SIN_OUTPUT].setChannels(currentPolyphony);
        outputs[SAW_OUTPUT].setChannels(currentPolyphony);
        outputs[PARA_OUTPUT].setChannels(currentPolyphony);

        //now we are going to look at our input and parameters and
        // save off some values for out audio processing function.
        outputSaw = outputs[SAW_OUTPUT].isConnected();
        outputSin = outputs[SIN_OUTPUT].isConnected();
        outputPara = outputs[PARA_OUTPUT].isConnected();
        float pitchParam = params[PITCH_PARAM].value;
        for (int i = 0; i < currentPolyphony; ++i) {
            float pitchCV = inputs[CV_INPUT].getVoltage(i);
            float combinedPitch = pitchParam + pitchCV - 4.f;

            const float q = float(std::log2(261.626));       // move up to C
            combinedPitch += q;

            // combined pitch is in volts. Now use the pow function
            // to convert that to a pitch. Not: there are more efficient ways
            // to do this than use std::pow.
            const float freq = std::pow(2.f, combinedPitch);

            // figure out how much to add to our ramp every cycle 
            // to make a saw at the desired frequency.
            const float normalizedFreq = args.sampleTime * freq;
            phaseAdvance[i] = normalizedFreq;
        }
    }

    void generateOutput() {
        for (int i = 0; i < currentPolyphony; ++i) {
            // Every sample, we advance the phase of our ramp by the amount
            // we derived from the CV and knob inputs.
            phaseAccumulators[i] += phaseAdvance[i];
            if (phaseAccumulators[i] > 1.f) {
                // We limit our phase to the range 0..1
                phaseAccumulators[i] -= 1.f;
            }

            if (outputSaw) {
                // If the saw output it patched, turn our 0..1 ramp
                // into a -5..+5 sawtooth. This  math is very easy!
                float sawWave = (phaseAccumulators[i] - .5f) * 10;
                outputs[SAW_OUTPUT].setVoltage(sawWave, i);
            }

            if (outputSin) {
                // If the sin output it patched, turn our 0..1 ramp
                // into a -5..+5 sine. This  math is less easy!

                // First convert 0..1 0..2pi (convert to radian angles)
                float radianPhase = phaseAccumulators[i] * 2 * float(M_PI);

                // sin of 0..2pi will be a sinewave from -1 to 1.
                // Easy to convert to -5 to +5
                float sinWave = std::sin(radianPhase) * 5;
                outputs[SIN_OUTPUT].setVoltage(sinWave, i);
            }

            if (outputPara) {
                // This simple "parabolic ramp" is an example of a way one could try to 
                // make the sawtooth sound a little different.
                // float paraWave = phaseAccumulators[i];
                // paraWave *= paraWave;
                // paraWave -= .33f;       // subtract out the DC component (use your calculus or trial and error).
                // paraWave *= 10;
                // outputs[PARA_OUTPUT].setVoltage(paraWave, i);

                indexToSampleFrom = std::floor(phaseAccumulators[i] * 16);
                float customValue = 0;

                if(indexToSampleFrom == 0) {
                  customValue = params[WAVE0_PARAM].value;
                } else if(indexToSampleFrom == 1) {
                  customValue = params[WAVE1_PARAM].value;
                } else if(indexToSampleFrom == 2) {
                  customValue = params[WAVE2_PARAM].value;
                } else if(indexToSampleFrom == 3) {
                  customValue = params[WAVE3_PARAM].value;
                } else if(indexToSampleFrom == 4) {
                  customValue = params[WAVE4_PARAM].value;
                } else if(indexToSampleFrom == 5) {
                  customValue = params[WAVE5_PARAM].value;
                } else if(indexToSampleFrom == 6) {
                  customValue = params[WAVE6_PARAM].value;
                } else if(indexToSampleFrom == 7) {
                  customValue = params[WAVE7_PARAM].value;
                } else if(indexToSampleFrom == 8) {
                  customValue = params[WAVE8_PARAM].value;
                } else if(indexToSampleFrom == 9) {
                  customValue = params[WAVE9_PARAM].value;
                } else if(indexToSampleFrom == 10) {
                  customValue = params[WAVE10_PARAM].value;
                } else if(indexToSampleFrom == 11) {
                  customValue = params[WAVE11_PARAM].value;
                } else if(indexToSampleFrom == 12) {
                  customValue = params[WAVE12_PARAM].value;
                } else if(indexToSampleFrom == 13) {
                  customValue = params[WAVE13_PARAM].value;
                } else if(indexToSampleFrom == 14) {
                  customValue = params[WAVE14_PARAM].value;
                } else if(indexToSampleFrom == 15) {
                  customValue = params[WAVE15_PARAM].value;
                }

                outputs[PARA_OUTPUT].setVoltage(customValue, i);
            }
        }
    }
};

/**
 * At least in VCV 1.0, every module must have a Widget, too.
 * The widget provides the user interface for a module.
 * Widgets may draw to the screen, get mouse and keyboard input, etc...
 * Widgets cannot actually process or generate audio.
 */
struct VCOCustomWidget : ModuleWidget {
    VCOCustomWidget(VCOCustomModule* module) {
        // The widget always retains a reference to the module.
        // you must call this function first in your widget constructor.
        setModule(module);

        // Typically the panel graphic is added first, then the other 
        // UI elements are placed on TOP.
        // In VCV the Z-order of added children is such that later
        // children are always higher than children added earlier.
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/vcoCustom_panel.svg")));

        // VCV modules usually have image is "screws" to make them
        // look more like physical module. You may design your own screws, 
        // or not use screws at all.
		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 30, 365)));

        // It's purely personal style whether you want to set variables like this
        // For the position of your widgets. It's fine to do it inline also.
        float x = 20;
        float headingY = 20;
        float inputY = 75;
        // float knobY = 130;
        // float sawY = 190;
        // float sinY = 240;
        float paraY = 120;
        float labelAbove = 20;
        float sliderX = 80;
        float sliderY = 50;
        float sliderSpacing = 20;

        // Now we place the widgets that represent the inputs, outputs, controls,
        // and lights for the module. VCOCustom does not have any lights, but does have
        // the other widgets.

        addInput(createInput<PJ301MPort>(Vec(x, inputY), module, VCOCustomModule::CV_INPUT));
        // addParam(createParam<RoundBlackKnob>(Vec(x-4, knobY), module, VCOCustomModule::PITCH_PARAM));
        
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(0*sliderSpacing), sliderY), module, VCOCustomModule::WAVE0_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(1*sliderSpacing), sliderY), module, VCOCustomModule::WAVE1_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(2*sliderSpacing), sliderY), module, VCOCustomModule::WAVE2_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(3*sliderSpacing), sliderY), module, VCOCustomModule::WAVE3_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(4*sliderSpacing), sliderY), module, VCOCustomModule::WAVE4_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(5*sliderSpacing), sliderY), module, VCOCustomModule::WAVE5_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(6*sliderSpacing), sliderY), module, VCOCustomModule::WAVE6_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(7*sliderSpacing), sliderY), module, VCOCustomModule::WAVE7_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(8*sliderSpacing), sliderY), module, VCOCustomModule::WAVE8_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(9*sliderSpacing), sliderY), module, VCOCustomModule::WAVE9_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(10*sliderSpacing), sliderY), module, VCOCustomModule::WAVE10_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(11*sliderSpacing), sliderY), module, VCOCustomModule::WAVE11_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(12*sliderSpacing), sliderY), module, VCOCustomModule::WAVE12_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(13*sliderSpacing), sliderY), module, VCOCustomModule::WAVE13_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(14*sliderSpacing), sliderY), module, VCOCustomModule::WAVE14_PARAM));
        addParam(createParam<BefacoSlidePot>(Vec(sliderX+(15*sliderSpacing), sliderY), module, VCOCustomModule::WAVE15_PARAM));

        // addOutput(createOutput<PJ301MPort>(Vec(x, sawY), module, VCOCustomModule::SAW_OUTPUT));
        // addOutput(createOutput<PJ301MPort>(Vec(x, sinY), module, VCOCustomModule::SIN_OUTPUT));
        addOutput(createOutput<PJ301MPort>(Vec(x, paraY), module, VCOCustomModule::PARA_OUTPUT));

        // Add some quick hack labels to the panel.
        addLabel(Vec(20, headingY), "Arbitrary Waveform Generator");
        addLabel(Vec(x-16, inputY - labelAbove), "Pitch CV");
        // addLabel(Vec(x-10, knobY - labelAbove), "Pitch");
        // addLabel(Vec(x-16, sawY - labelAbove), "Saw Out");
        // addLabel(Vec(x-16, sinY - labelAbove), "Sin Out");
        addLabel(Vec(x-16, paraY - labelAbove), "Output");
    }

    // Simple helper function to add test labels to the panel.
    // In a real module you would draw this on the panel itself.
    // Labels are fine for hacking, but they are discouraged for real use.
    // Some of the problems are that they don't draw particularly efficiently,
    // and they don't give as much control as putting them into the panel SVG.
    Label* addLabel(const Vec& v, const std::string& str)
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

// This mysterious line must appear for each module. The
// name in quotes at then end is the same string that will be in 
// plugin.json in the entry for corresponding plugin.

// This line basically tells VCV Rack:
// I'm called "demo-vc-custom", my module is VCOCustomModule, and my Widget is VCOCustomWidget.
// In effect, it implements a module factory.
Model* modelVCOCustom = createModel<VCOCustomModule, VCOCustomWidget>("demo-vco-custom");