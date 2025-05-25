// Full Arduino Cloud-style Chart Dashboard with Switches and Local Cache

import React, { useEffect, useState } from 'react';
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer,
  CartesianGrid,
} from 'recharts';
import { Button } from '@/components/ui/button';

const ranges = ['15 D', '7 D', '1 D', '1 H', 'LIVE'];

const generateFakeData = (range) => {
  const now = new Date();
  const count = {
    '15 D': 200,
    '7 D': 150,
    '1 D': 100,
    '1 H': 60,
    LIVE: 20,
  }[range];

  return Array.from({ length: count }, (_, i) => {
    const timestamp = new Date(now.getTime() - (count - i) * 1000);
    return {
      time: timestamp.toLocaleTimeString(),
      value: 3 + Math.random() * 4,
    };
  });
};

export default function Dashboard() {
  const [range, setRange] = useState('LIVE');
  const [data, setData] = useState([]);

  // Load cached data
  useEffect(() => {
    const cached = localStorage.getItem(`iot-data-${range}`);
    if (cached) {
      setData(JSON.parse(cached));
    } else {
      const generated = generateFakeData(range);
      setData(generated);
      localStorage.setItem(`iot-data-${range}`, JSON.stringify(generated));
    }
  }, [range]);

  // Auto update for LIVE
  useEffect(() => {
    if (range === 'LIVE') {
      const interval = setInterval(() => {
        const newPoint = {
          time: new Date().toLocaleTimeString(),
          value: 3 + Math.random() * 4,
        };
        setData((prev) => {
          const updated = [...prev.slice(-19), newPoint];
          localStorage.setItem(`iot-data-LIVE`, JSON.stringify(updated));
          return updated;
        });
      }, 2000);
      return () => clearInterval(interval);
    }
  }, [range]);

  return (
    <div className="p-4 bg-slate-100 rounded-xl">
      <div className="flex justify-between items-center">
        <h2 className="text-md font-semibold">Chart</h2>
        <span className="bg-yellow-100 px-2 py-1 rounded text-xs font-mono text-yellow-800">Example</span>
      </div>

      <div className="grid grid-cols-5 gap-1 mt-2 rounded overflow-hidden">
        {ranges.map((r) => (
          <button
            key={r}
            onClick={() => setRange(r)}
            className={`text-sm py-1 ${
              range === r ? 'bg-teal-600 text-white' : 'bg-white text-gray-800'
            }`}
          >
            {r}
          </button>
        ))}
      </div>

      <div className="h-80 mt-2">
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data} margin={{ top: 20, right: 20, bottom: 10, left: 0 }}>
            <CartesianGrid stroke="#e0e0e0" strokeDasharray="3 3" />
            <XAxis dataKey="time" tick={{ fontSize: 10 }} />
            <YAxis domain={[2, 7]} tick={{ fontSize: 12 }} />
            <Tooltip />
            <Line
              type="monotone"
              dataKey="value"
              stroke="#009CA6"
              strokeWidth={2}
              dot={{ r: 4, stroke: '#009CA6', strokeWidth: 2, fill: 'white' }}
              activeDot={{ r: 5 }}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}
