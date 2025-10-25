import numpy as np

class Meals:
    def __init__(self, meals, params=None):
        self.meals = meals
        self.params = params
        
    def update_meals(self, meals):
        self.meals = meals
        
    def update_params(self, params):
        self.params = params
        # TODO: Implement the update_params method
        
    def add_meal(self, meal):
        self.meals.append(meal)
    
    def __call__(self, t):
        # parameters and model are from Coman, S., and C. Boldisor. "Simulation of an adaptive closed loop system for blood glucose concentration control." 
        # Bulletin of the Transilvania University of Brasov. Series I-Engineering Sciences (2015): 107-112.
        total_glucose_bolus = 0
        for i in range(len(self.meals)):
            meal_start, meal_size, absorption_duration = self.meals[i]
            meal_end = meal_start + absorption_duration
            if meal_end > t >= meal_start:
                D_g = meal_size  # [g] is the quantity of carbohydrates in the meal
                A_g = 0.8  # constant in the model
                t_max = 40  # [min] is the time at which the glucose absorption rate is maximum
                absorption_rate = D_g * A_g * (t - meal_start) * np.exp(-(t - meal_start)/t_max) / t_max**2
                total_glucose_bolus += absorption_rate
        return total_glucose_bolus

