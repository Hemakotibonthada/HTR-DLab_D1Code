// React Home Automation Dashboard (No MQTT)
// Features: Login, Relay Control, Sensor Charts, WebSocket Sync

import React, { useEffect, useState } from 'react';
import { LineChart, Line, XAxis, YAxis, Tooltip, ResponsiveContainer } from 'recharts';
import { Button } from '@/components/ui/button';

const WS_URL = 'ws://' + window.location.hostname + '/ws';
const API_LOGIN = '/api/login';

export default function Dashboard() {
  const [loggedIn, setLoggedIn] = useState(false);
  const [username, setUsername] = useState('');
  const [password, setPassword] = useState('');
  const [relays, setRelays] = useState(Array(8).fill(false));
  const [temp, setTemp] = useState(0);
  const [hum, setHum] = useState(0);
  const [socket, setSocket] = useState(null);
  const [chartData, setChartData] = useState([]);
  const [timer, setTimer] = useState(null);

  useEffect(() => {
    const ws = new WebSocket(WS_URL);
    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.type === 'update') {
        setRelays(msg.relays);
        setTemp(msg.temp);
        setHum(msg.hum);
        setChartData(prev => [...prev.slice(-199), {
          time: new Date().toLocaleTimeString(),
          temp: msg.temp,
          hum: msg.hum
        }]);
      }
    };
    setSocket(ws);
    return () => ws.close();
  }, []);

  const handleLogin = async () => {
    const res = await fetch(API_LOGIN, {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: new URLSearchParams({ username, password })
    });
    if (res.ok) setLoggedIn(true);
    else alert('Login failed');
  };

  const toggleRelay = (i) => {
    const newState = !relays[i];
    socket.send(JSON.stringify({ type: 'relay', id: i, state: newState }));
  };

  if (!loggedIn) {
    return (
      <div className="h-screen flex flex-col justify-center items-center gap-4">
        <h1 className="text-3xl font-bold">Login</h1>
        <input className="border p-2" placeholder="Username" onChange={e => setUsername(e.target.value)} />
        <input className="border p-2" placeholder="Password" type="password" onChange={e => setPassword(e.target.value)} />
        <Button onClick={handleLogin}>Login</Button>
      </div>
    );
  }

  return (
    <div className="p-4 max-w-4xl mx-auto">
      <h1 className="text-xl font-bold mb-4">Home Dashboard</h1>

      <div className="grid grid-cols-2 md:grid-cols-4 gap-4 mb-8">
        {relays.map((r, i) => (
          <Button key={i} onClick={() => toggleRelay(i)} className={r ? 'bg-green-500' : 'bg-red-500'}>
            Relay {i + 1}: {r ? 'ON' : 'OFF'}
          </Button>
        ))}
      </div>

      <div className="bg-white dark:bg-gray-900 rounded-xl shadow-md p-4">
        <h2 className="text-lg font-semibold mb-2">Temperature & Humidity</h2>
        <ResponsiveContainer width="100%" height={300}>
          <LineChart data={chartData} margin={{ top: 10, right: 20, bottom: 0, left: 0 }}>
            <XAxis dataKey="time" hide={true} />
            <YAxis domain={[0, 100]} hide={true} />
            <Tooltip />
            <Line type="monotone" dataKey="temp" stroke="#ff7300" dot={false} strokeWidth={2} />
            <Line type="monotone" dataKey="hum" stroke="#387908" dot={false} strokeWidth={2} />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
