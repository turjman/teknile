% SPDX-License-Identifier: Apache-2.0
% evre_studio_demo.m - EVRe Studio's JSON API from MATLAB (R2020b or newer:
% tcpclient, writeline, readline, jsonencode, jsondecode).
%
% EVRe Studio must be connected to the device with "Serve API" ticked.
% Writes also need "Allow API writes" (and "including ⚠ registers" for those).
% The register names are those of maps/example_device.json.

c = tcpclient("127.0.0.1", 1220, "Timeout", 3);
configureTerminator(c, "LF");
ask = @(q) jsondecode(char(evre_request(c, q)));

% what is connected
info = ask(struct("cmd", "info"));
fprintf("%s via %s, %d registers\n", info.device, info.link, info.registers);

% fresh values by name, scaled as the device map says
r = ask(struct("cmd", "get", "names", {{"SUPPLY_V", "SUPPLY_I", "STATE"}}));
fprintf("SUPPLY_V = %.3f V, SUPPLY_I = %.3f A\n", r.values.SUPPLY_V, r.values.SUPPLY_I);
if isfield(r, "decoded"), disp(r.decoded.STATE); end

% a write, read back (refused while "Allow API writes" is off)
w = ask(struct("cmd", "set", "values", struct("LED_MODE", 2)));
if w.ok, fprintf("LED_MODE is now %d\n", w.values.LED_MODE); else, disp(w.error); end

% a 5 s stream at 20 ms into arrays, then a plot
ask(struct("cmd", "stream", "names", {{"SUPPLY_V", "SUPPLY_I"}}, "ms", 20));
t = []; volts = []; amps = [];
t_end = tic;
while toc(t_end) < 5
    m = jsondecode(char(readline(c)));
    if isfield(m, "values")
        t(end+1) = m.t; volts(end+1) = m.values.SUPPLY_V; amps(end+1) = m.values.SUPPLY_I; %#ok<AGROW>
    end
end
writeline(c, jsonencode(struct("cmd", "stop")));
clear c   % closes the connection (and any stream left running)

t = t - t(1);
yyaxis left;  plot(t, volts); ylabel("SUPPLY\_V [V]");
yyaxis right; plot(t, amps);  ylabel("SUPPLY\_I [A]");
xlabel("time [s]"); grid on; title("EVRe Studio stream");

function line = evre_request(c, q)
    writeline(c, jsonencode(q));
    line = readline(c);
end
