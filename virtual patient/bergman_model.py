import numpy as np


class Bergman:
    def __init__(self, type='diabetic', params=None, meals=None, insulin=None):
        self.init_params(type, params)
        if meals is not None:
            self.meals = meals
        if insulin is not None:
            self.insulin = insulin
        
    def ode(self, t, x):
        '''
        @param t: time
        @param x: state variables (G, X, I)
        @param params: parameters (p1, p2, p3, n, gamma, h, Gb, Ib)
        @return: derivatives of state variables
        ----------------
        G: plasma glucose concentration [mg/dL]. Represents the glucose level in the bloodstream.
        X: dynamic insulin response.  Represents the effectiveness of insulin in promoting glucose uptake by tissues.
        I: plasma insulin concentration [µU/mL]. Represents the insulin level in the bloodstream.
        p1: glucose effectiveness (min^-1). Represents the rate at which glucose is utilized or cleared from the bloodstream without the effect of insulin.
        p2: rate constant for the decrease of insulin action (min^-1).  Reflects how quickly the effect of insulin diminishes over time.
        p3: Rate constant for insulin-stimulated glucose disposal ( min^-2(µU/ml)^-1 ). Describes how effectively insulin promotes glucose uptake by tissues.
        n: insulin clearance rate [min^-1]. Represents the rate at which insulin is cleared from the bloodstream.
        gamma: pancreatic responsivity [mg/dL per µU/mL per min]
        h: glucose threshold for pancreatic responsivity [mg/dL]. The glucose level at which the pancreas begins to secrete insulin. 
        Gb: basal glucose level [mg/dL]. The steady-state (fasting) glucose level when no external inputs are affecting the system.
        Ib: basal insulin level [µU/mL]. The steady-state insulin level when no external inputs are affecting the system.
        ----------------
        Reference: Mathematical modeling of the glucose–insulin system: A review, Equation (26)(28)
        '''
        # Unpack state variables
        G, X, I = x
        
        # Unpack parameters
        p1, p2, p3, n, gamma, h, Gb, Ib = self.params['p1'], self.params['p2'], self.params['p3'], self.params['n'], self.params['gamma'], self.params['h'], self.params['Gb'], self.params['Ib']
        
        # Calculate derivatives
        dGdt = -(p1 + X) * G + p1 * Gb + (self.meals(t) if hasattr(self, 'meals') else 0)
        dXdt = -p2 * X + p3 * (I - Ib)
        dIdt = -n * (I - Ib) + (gamma * t * (G - h) if G > h else 0) + (self.insulin(t) if hasattr(self, 'insulin') else 0)
        
        return np.array([dGdt, dXdt, dIdt])    

    
    def init_params(self, type='diabetic', params=None):
        '''
        Initialize model parameters based on the type of patient (normal, diabetic, or custom).
        '''
        # Parameters Reference: Minimal Models for Glucose and Insulin Kinetics - A Matlab Implementation
        normal_params = {  
            'p1': 2.6e-2,
            'p2': 2.5e-2,
            'p3': 1.25e-5,
            'n': 0.27,
            'gamma': 0.0041,
            'h': 100,
            'Gb': 92,
            'Ib': 11
        }
        diabetic_params = {
            'p1': 1.7e-2,
            'p2': 1e-2,
            'p3': 7e-7,
            'n': 0.27,
            'gamma': 0.0000001,
            'h': 100,
            'Gb': 92,
            'Ib': 11
        }
        if type == 'custom':
            if params is None:
                raise ValueError("Custom parameters must be provided.")
            self.params = params 
        elif type == 'normal':
            self.params = normal_params
        elif type == 'diabetic':
            self.params = diabetic_params
        else:    
            raise ValueError(f"Invalid type: {type}")
        
    def update_params(self, params):
        params_choices = ['p1', 'p2', 'p3', 'n', 'gamma', 'h', 'Gb', 'Ib']
        for key in params:
            if key in params_choices:
                self.params[key] = float(params[key])
            else:
                raise ValueError(f"Invalid parameter: {key}")
        
    def update_meals(self, meals):
        self.meals = meals

    def update_insulin(self, insulin):
        self.insulin = insulin







