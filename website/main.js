// main.js — Web App Entry for ESP32 UI (Vanilla Build for SPIFFS)

import React from 'react';
import { createRoot } from 'react-dom/client';
import Dashboard from './Dashboard';

const root = createRoot(document.getElementById('root'));
root.render(<Dashboard />);
