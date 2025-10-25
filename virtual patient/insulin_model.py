import numpy as np

'''
The approach to modeling insulin absorption using an exponential decay function comes from well-established pharmacokinetics and pharmacodynamics principles, 
particularly for drugs that follow first-order kinetics. This is widely applied in the modeling of insulin kinetics for rapid-acting insulins like Lispro. - ChatGPT

u_{inj}(t) = \frac{D \times 1e6}{V_d \times \tau} e^{-\frac{t - t_{inj}}{\tau}} H(t - t_{inj})
D is the insulin dose in IU.
V_d is the volume of distribution in mL.
𝜏 is the absorption time constant (in minutes).
t_inj is the time of insulin injection.
H(t−t_inj) is the Heaviside step function, ensuring the insulin starts being absorbed after the injection time.
'''

class Insulin:
    def __init__(self, V_d=12000, bolus_insulin=None, U_basal=2.0/60):
        self.V_d = V_d   # distribution volume [mL] (blood volume)
        if bolus_insulin is not None:
            self.bolus_insulin = bolus_insulin
        self.U_basal = U_basal
    
    def update_basal_rate(self, U_basal):
        self.U_basal = U_basal
        
    def u_bolus(self, t):
        u = 0            
        for i in range(len(self.bolus_insulin)):
            bolus_times, bolus_doses, tau = self.bolus_insulin[i]   # bolus_doses: insulin dose [µU/mL], bolus_times: time of insulin injection [min], tau: absorption time constant [min]
            if bolus_times+3*tau > t >= bolus_times:
                u += bolus_doses * 1e6 / tau / self.V_d * np.exp(-(t - bolus_times) / tau)  # U -> [µU/mL]
        return u
    
    def __call__(self, t):
        return self.U_basal * 1e6 / self.V_d + self.u_bolus(t)
    
    