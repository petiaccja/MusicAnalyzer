sampleRate = 48000;
cutoff = 8000;
length = 0.002;

numSamples = floor(sampleRate*length/2)*2+1;

t = 0:numSamples-1;
t = t/(numSamples-1);
t = t*length;
t = t-(length/2);

n = 2*cutoff;
y = sin(pi*n*t)./(pi*n*t);
y(floor(numSamples/2) + 1) = 1;

w = 0.54 + 0.46*cos(2*t/length*pi);

y = y.* w;
y = y / sum(y);

figure(1);
plot(t, [y' w']);
fftsize = 16384;
figure(2);
plot((0:fftsize-1)./(fftsize-1).*sampleRate, abs(fft(y, fftsize)));




