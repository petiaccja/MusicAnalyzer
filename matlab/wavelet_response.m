sampleRate = 44100;
freqs = [40, 55, 70, 85, 105, 125, 145, 155, 180, 200, 225, 250, 275, 300];
lengths = [0.12, 0.10, 0.10, 0.10, 0.09, 0.09, 0.09, 0.08, 0.10, 0.10, 0.08, 0.08, 0.08, 0.08];
samplePeriod = 1/sampleRate;

fftsize = 32768;
clf(figure(1));
clf(figure(2));

for i=1:length(freqs)
    % calculate wavelet function
    f = freqs(i);
    l = lengths(i);
    taps = ceil(l/samplePeriod);
    
    t = 0:taps-1;
    t = t-floor(taps/2);
    t = t*samplePeriod;
    
    var = l/3;
    varsq = var*var;
    
    w = exp(-t.^2 / varsq); % gaussian window
    %w = 0.54+0.46*cos(2*pi*t/l); % hamming window
    s = exp(1i*f*2*pi*t);
    y = w .* s;
    y = y/sum(abs(y));
    
    figure(2);
    hold on;
    plot(t,w);
    
    % plot fft
    figure(1);
    hold on;
    pt = (0:fftsize-1)/fftsize*sampleRate;
    pfft = abs(fft(y, fftsize));
    plot(pt(1:floor(400*fftsize/sampleRate)), pfft(1:floor(400*fftsize/sampleRate)));
end
